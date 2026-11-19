#include "aegisproxy/network/epoll_poller.hpp"

#include <algorithm>
#include <cerrno>

#include <sys/epoll.h>

namespace aegis::net {

namespace {

std::uint32_t to_epoll(EventMask mask) noexcept {
  std::uint32_t ev = 0;
  if (has(mask, EventMask::Readable))
    ev |= EPOLLIN;
  if (has(mask, EventMask::Writable))
    ev |= EPOLLOUT;
  // EPOLLERR / EPOLLHUP are always reported by the kernel; no need to request them.
  return ev;
}

EventMask from_epoll(std::uint32_t ev) noexcept {
  EventMask mask = EventMask::None;
  if ((ev & EPOLLIN) != 0U)
    mask |= EventMask::Readable;
  if ((ev & EPOLLOUT) != 0U)
    mask |= EventMask::Writable;
  if ((ev & EPOLLERR) != 0U)
    mask |= EventMask::Error;
  if ((ev & (EPOLLHUP | EPOLLRDHUP)) != 0U)
    mask |= EventMask::HangUp;
  return mask;
}

epoll_event make_event(EventMask interest, HandlerId token) noexcept {
  epoll_event ev{};
  ev.events = to_epoll(interest);
  ev.data.u64 = token;
  return ev;
}

}  // namespace

Result<std::unique_ptr<EpollPoller>> EpollPoller::create() {
  SocketHandle fd{::epoll_create1(EPOLL_CLOEXEC)};
  if (!fd)
    return Error::from_errno("epoll_create1");
  return std::unique_ptr<EpollPoller>(new EpollPoller(std::move(fd)));
}

Status EpollPoller::add(Fd fd, EventMask interest, HandlerId token) {
  epoll_event ev = make_event(interest, token);
  if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &ev) < 0) {
    return Error::from_errno("epoll_ctl(ADD)");
  }
  return ok();
}

Status EpollPoller::modify(Fd fd, EventMask interest, HandlerId token) {
  epoll_event ev = make_event(interest, token);
  if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &ev) < 0) {
    return Error::from_errno("epoll_ctl(MOD)");
  }
  return ok();
}

Status EpollPoller::remove(Fd fd) {
  // The kernel removes a closed fd automatically, so EBADF/ENOENT after close() are not
  // errors for our purposes; EventLoop always removes before the SocketHandle closes.
  if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr) < 0) {
    if (errno == EBADF || errno == ENOENT)
      return ok();
    return Error::from_errno("epoll_ctl(DEL)");
  }
  return ok();
}

Result<std::size_t> EpollPoller::wait(std::span<PollEvent> out, std::chrono::milliseconds timeout) {
  if (out.empty())
    return std::size_t{0};
  epoll_event raw[256];
  const int max_events = static_cast<int>(std::min<std::size_t>(out.size(), 256));
  const int timeout_ms = timeout.count() < 0 ? -1 : static_cast<int>(timeout.count());
  const int n = ::epoll_wait(epoll_fd_.get(), raw, max_events, timeout_ms);
  if (n < 0) {
    if (errno == EINTR)
      return std::size_t{0};
    return Error::from_errno("epoll_wait");
  }
  for (int i = 0; i < n; ++i) {
    out[static_cast<std::size_t>(i)] = PollEvent{raw[i].data.u64, from_epoll(raw[i].events)};
  }
  return static_cast<std::size_t>(n);
}

}  // namespace aegis::net
