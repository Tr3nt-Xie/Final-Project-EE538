#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"
#include "aegisproxy/network/poller.hpp"
#include "aegisproxy/network/socket_handle.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aegis::net {

/// Single-threaded readiness dispatcher.
///
/// Responsibilities (and nothing more): own the Poller, map HandlerId -> callback,
/// translate readiness into callback invocations, and run deferred tasks after each
/// dispatch round. It knows nothing about HTTP, connections, or backends.
///
/// Stale-event safety: callbacks are looked up by HandlerId for every event. If a
/// handler was removed earlier in the same dispatch round (e.g. a connection closed
/// itself), later events carrying that id are dropped. Because ids are never reused,
/// an fd number being recycled by the kernel cannot route an old event to a new owner.
///
/// Re-entrancy: callbacks may call add/modify/remove/defer/stop on the loop. The
/// loop holds a shared_ptr to the entry while invoking its callback, so a callback
/// that removes itself does not destroy the std::function it is executing.
///
/// Thread-safety: none — all methods must be called from the thread running the loop.
/// (stop() is the only method that is also safe to call from a signal handler-driven
/// callback, because it is invoked on the loop thread via signalfd; see main.cpp.)
class EventLoop {
 public:
  using Callback = std::function<void(EventMask)>;
  using Task = std::function<void()>;

  /// Builds a loop around the platform default poller (EpollPoller on Linux).
  /// Error Unsupported elsewhere.
  static Result<std::unique_ptr<EventLoop>> create(std::size_t max_events_per_round = 256);

  explicit EventLoop(std::unique_ptr<Poller> poller, std::size_t max_events_per_round = 256);
  ~EventLoop();

  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;
  EventLoop(EventLoop&&) = delete;
  EventLoop& operator=(EventLoop&&) = delete;

  /// Registers `fd` for `interest`. The loop does NOT take ownership of the fd; the
  /// caller must keep its SocketHandle alive until remove() returns.
  Result<HandlerId> add(Fd fd, EventMask interest, Callback callback);
  Status modify(HandlerId id, EventMask interest);
  Status remove(HandlerId id);

  [[nodiscard]] bool contains(HandlerId id) const noexcept;
  [[nodiscard]] std::size_t handler_count() const noexcept { return handlers_.size(); }

  /// Queues work to run after the current dispatch round (used to destroy objects
  /// safely from inside their own callbacks).
  void defer(Task task);

  /// One poll + dispatch round. Returns the number of events dispatched (dropped
  /// stale events are not counted).
  Result<std::size_t> run_once(std::chrono::milliseconds timeout);

  /// Runs until stop() is called. Returns the first fatal poll error, if any.
  Status run();

  /// Requests run() to return after the current round; wakes a blocked poll.
  void stop() noexcept;

  [[nodiscard]] bool is_running() const noexcept { return running_; }
  [[nodiscard]] std::uint64_t stale_events_dropped() const noexcept { return stale_dropped_; }

 private:
  struct Entry {
    Fd fd;
    EventMask interest;
    Callback callback;
  };

  Status install_wake_pipe();
  void run_deferred();

  std::unique_ptr<Poller> poller_;
  std::unordered_map<HandlerId, std::shared_ptr<Entry>> handlers_;
  HandlerId next_id_{1};
  std::vector<PollEvent> events_;
  std::vector<Task> deferred_;
  bool running_{false};
  bool stop_requested_{false};
  std::uint64_t stale_dropped_{0};

  // Self-pipe used by stop() to interrupt a blocking wait.
  SocketHandle wake_read_;
  SocketHandle wake_write_;
  HandlerId wake_handler_{kInvalidHandlerId};
};

}  // namespace aegis::net
