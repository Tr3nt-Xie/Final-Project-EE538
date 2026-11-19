#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"

#include <cerrno>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace aegis::net {

/// RAII owner of a file descriptor. Move-only; closes on destruction.
///
/// Ownership rule: exactly one SocketHandle owns a given fd. Pass raw `Fd` values only
/// for the duration of a call (e.g. to the poller); never store a raw fd across calls.
/// A moved-from handle is empty (`valid() == false`) and safe to destroy.
class SocketHandle {
 public:
  SocketHandle() noexcept = default;
  explicit SocketHandle(Fd fd) noexcept : fd_(fd) {}
  ~SocketHandle();

  SocketHandle(const SocketHandle&) = delete;
  SocketHandle& operator=(const SocketHandle&) = delete;

  SocketHandle(SocketHandle&& other) noexcept : fd_(std::exchange(other.fd_, kInvalidFd)) {}
  SocketHandle& operator=(SocketHandle&& other) noexcept;

  [[nodiscard]] Fd get() const noexcept { return fd_; }
  [[nodiscard]] bool valid() const noexcept { return fd_ != kInvalidFd; }
  explicit operator bool() const noexcept { return valid(); }

  /// Gives up ownership without closing. Caller becomes responsible for the fd.
  [[nodiscard]] Fd release() noexcept { return std::exchange(fd_, kInvalidFd); }

  /// Closes the current fd (if any) and takes ownership of `fd`.
  void reset(Fd fd = kInvalidFd) noexcept;

 private:
  Fd fd_{kInvalidFd};
};

/// Thin, non-throwing wrappers over the BSD socket API. All sockets created here are
/// non-blocking and close-on-exec. IPv4 literals only for now (see TODO in .cpp).
namespace socket_ops {

/// True for the errno values that mean "retry when the socket is ready".
/// (EAGAIN == EWOULDBLOCK on Linux and macOS, but POSIX does not require it.)
[[nodiscard]] constexpr bool is_would_block(int err) noexcept {
#if EAGAIN == EWOULDBLOCK
  return err == EAGAIN;
#else
  return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

Status set_non_blocking(Fd fd);
Status set_close_on_exec(Fd fd);
Status set_reuse_addr(Fd fd);
Status set_tcp_nodelay(Fd fd);

/// socket(AF_INET, SOCK_STREAM) configured non-blocking + CLOEXEC.
Result<SocketHandle> create_tcp_socket();

/// Creates, binds and listens. `host` must be an IPv4 literal ("0.0.0.0", "127.0.0.1").
/// `port` may be 0 to let the kernel choose (use local_port() to read it back).
Result<SocketHandle> listen_tcp(std::string_view host, std::uint16_t port, int backlog);

/// Non-blocking accept. Returns std::nullopt when no connection is pending (EAGAIN).
/// The accepted socket is non-blocking + CLOEXEC.
Result<std::optional<SocketHandle>> accept_tcp(Fd listener);

enum class ConnectState : std::uint8_t { Connected, InProgress };

/// Starts a non-blocking connect. InProgress means: wait for Writable, then call
/// pending_error() to learn whether it succeeded.
Result<std::pair<SocketHandle, ConnectState>> connect_tcp(std::string_view host,
                                                          std::uint16_t port);

/// SO_ERROR: 0 on success, otherwise the errno of the failed async connect.
Result<int> pending_error(Fd fd);

/// Port the socket is bound to (useful after listen_tcp with port 0).
Result<std::uint16_t> local_port(Fd fd);

}  // namespace socket_ops

}  // namespace aegis::net
