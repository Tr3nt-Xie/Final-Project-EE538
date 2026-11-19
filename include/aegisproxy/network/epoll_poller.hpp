#pragma once

#include "aegisproxy/network/poller.hpp"
#include "aegisproxy/network/socket_handle.hpp"

#include <memory>

namespace aegis::net {

/// Linux epoll(7) implementation of Poller. Level-triggered by design for the initial
/// framework (simpler reasoning: an fd stays ready until drained; no risk of missing
/// a wake-up). Switching to edge-triggered is a Member 1 performance task once the
/// read/write loops drain to EAGAIN reliably.
///
/// Only compiled when AEGIS_HAVE_EPOLL is defined (Linux).
class EpollPoller final : public Poller {
 public:
  /// Error Unsupported on non-Linux, SystemError if epoll_create1 fails.
  static Result<std::unique_ptr<EpollPoller>> create();

  ~EpollPoller() override = default;

  Status add(Fd fd, EventMask interest, HandlerId token) override;
  Status modify(Fd fd, EventMask interest, HandlerId token) override;
  Status remove(Fd fd) override;
  Result<std::size_t> wait(std::span<PollEvent> out, std::chrono::milliseconds timeout) override;

  [[nodiscard]] Fd fd() const noexcept { return epoll_fd_.get(); }

 private:
  explicit EpollPoller(SocketHandle epoll_fd) : epoll_fd_(std::move(epoll_fd)) {}

  SocketHandle epoll_fd_;  ///< SocketHandle is just an fd owner; fine for the epoll fd too.
};

}  // namespace aegis::net
