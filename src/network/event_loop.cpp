#include "aegisproxy/network/event_loop.hpp"

#include "aegisproxy/common/log.hpp"

#include <cerrno>
#include <string>
#include <utility>

#include <unistd.h>
#if defined(AEGIS_HAVE_EPOLL)
#include "aegisproxy/network/epoll_poller.hpp"
#endif

namespace aegis::net {

namespace {
constexpr std::string_view kLog = "event_loop";
}

Result<std::unique_ptr<EventLoop>> EventLoop::create(std::size_t max_events_per_round) {
#if defined(AEGIS_HAVE_EPOLL)
  auto poller = EpollPoller::create();
  if (!poller)
    return poller.error();
  return std::make_unique<EventLoop>(std::move(*poller), max_events_per_round);
#else
  (void)max_events_per_round;
  return Error{ErrorCode::Unsupported,
               "no default Poller on this platform (epoll is Linux-only); "
               "build on Linux or inject a Poller explicitly"};
#endif
}

EventLoop::EventLoop(std::unique_ptr<Poller> poller, std::size_t max_events_per_round)
    : poller_(std::move(poller)), events_(max_events_per_round == 0 ? 1 : max_events_per_round) {
  if (Status st = install_wake_pipe(); !st) {
    // A loop without a wake pipe still works; stop() then takes effect after the next
    // poll timeout. Logged rather than fatal so that unit tests with fake pollers run.
    log::warn(kLog, "wake pipe unavailable: " + st.error().to_string());
  }
}

EventLoop::~EventLoop() {
  if (wake_handler_ != kInvalidHandlerId) {
    (void)remove(wake_handler_);
  }
}

Status EventLoop::install_wake_pipe() {
  int fds[2] = {kInvalidFd, kInvalidFd};
  if (::pipe(fds) < 0)
    return Error::from_errno("pipe");
  wake_read_.reset(fds[0]);
  wake_write_.reset(fds[1]);
  for (const Fd fd : fds) {
    if (Status st = socket_ops::set_non_blocking(fd); !st)
      return st;
    if (Status st = socket_ops::set_close_on_exec(fd); !st)
      return st;
  }
  auto id = add(wake_read_.get(), EventMask::Readable, [this](EventMask) {
    char drain[64];
    while (::read(wake_read_.get(), drain, sizeof drain) > 0) {
    }
  });
  if (!id)
    return id.error();
  wake_handler_ = *id;
  return ok();
}

Result<HandlerId> EventLoop::add(Fd fd, EventMask interest, Callback callback) {
  if (fd == kInvalidFd)
    return Error{ErrorCode::InvalidArgument, "add: invalid fd"};
  if (!callback)
    return Error{ErrorCode::InvalidArgument, "add: empty callback"};
  const HandlerId id = next_id_++;
  if (Status st = poller_->add(fd, interest, id); !st)
    return st.error();
  handlers_.emplace(id, std::make_shared<Entry>(Entry{fd, interest, std::move(callback)}));
  return id;
}

Status EventLoop::modify(HandlerId id, EventMask interest) {
  auto it = handlers_.find(id);
  if (it == handlers_.end()) {
    return Error{ErrorCode::NotFound, "modify: unknown handler " + std::to_string(id)};
  }
  if (Status st = poller_->modify(it->second->fd, interest, id); !st)
    return st;
  it->second->interest = interest;
  return ok();
}

Status EventLoop::remove(HandlerId id) {
  auto it = handlers_.find(id);
  if (it == handlers_.end()) {
    return Error{ErrorCode::NotFound, "remove: unknown handler " + std::to_string(id)};
  }
  const Fd fd = it->second->fd;
  handlers_.erase(it);
  return poller_->remove(fd);
}

bool EventLoop::contains(HandlerId id) const noexcept {
  return handlers_.contains(id);
}

void EventLoop::defer(Task task) {
  if (task)
    deferred_.push_back(std::move(task));
}

void EventLoop::run_deferred() {
  // Tasks may enqueue further tasks; swap first so iteration stays well-defined.
  while (!deferred_.empty()) {
    std::vector<Task> batch;
    batch.swap(deferred_);
    for (Task& t : batch)
      t();
  }
}

Result<std::size_t> EventLoop::run_once(std::chrono::milliseconds timeout) {
  auto n = poller_->wait(events_, timeout);
  if (!n)
    return n.error();

  std::size_t dispatched = 0;
  for (std::size_t i = 0; i < *n; ++i) {
    const PollEvent& ev = events_[i];
    auto it = handlers_.find(ev.token);
    if (it == handlers_.end()) {
      ++stale_dropped_;  // Handler removed earlier this round or fd recycled: drop.
      continue;
    }
    std::shared_ptr<Entry> entry = it->second;  // keep alive across the callback
    entry->callback(ev.events);
    ++dispatched;
  }
  run_deferred();
  return dispatched;
}

Status EventLoop::run() {
  if (running_)
    return Error{ErrorCode::InvalidState, "run: loop already running"};
  running_ = true;
  Status result = ok();
  // A stop() issued before run() is honoured (the flag is sticky until run() exits),
  // which matters when a signal arrives during startup.
  while (!stop_requested_) {
    auto r = run_once(std::chrono::milliseconds{-1});
    if (!r) {
      result = r.error();
      break;
    }
  }
  run_deferred();
  stop_requested_ = false;
  running_ = false;
  return result;
}

void EventLoop::stop() noexcept {
  stop_requested_ = true;
  if (wake_write_) {
    const char byte = 1;
    (void)!::write(wake_write_.get(), &byte, 1);  // EAGAIN (pipe full) is fine: already woken.
  }
}

}  // namespace aegis::net
