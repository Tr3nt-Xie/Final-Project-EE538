#include "aegisproxy/network/socket_handle.hpp"

#include <cerrno>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aegis::net {

SocketHandle::~SocketHandle() {
  reset();
}

SocketHandle& SocketHandle::operator=(SocketHandle&& other) noexcept {
  if (this != &other) {
    reset(other.release());
  }
  return *this;
}

void SocketHandle::reset(Fd fd) noexcept {
  if (fd_ != kInvalidFd) {
    // Ignore the return value: there is nothing sensible to do on close() failure in a
    // destructor, and retrying on EINTR is unsafe on Linux (the fd is already released).
    ::close(fd_);
  }
  fd_ = fd;
}

namespace socket_ops {

namespace {

Result<sockaddr_in> make_ipv4(std::string_view host, std::uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  const std::string host_z(host);
  // TODO(Member 1): resolve hostnames (getaddrinfo) — blocking DNS does not belong on
  // the event loop, so either resolve at config-load time or add an async resolver.
  if (::inet_pton(AF_INET, host_z.c_str(), &addr.sin_addr) != 1) {
    return Error{ErrorCode::InvalidArgument,
                 "'" + host_z + "' is not an IPv4 literal (hostname resolution is a TODO)"};
  }
  return addr;
}

}  // namespace

Status set_non_blocking(Fd fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return Error::from_errno("fcntl(F_GETFL)");
  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return Error::from_errno("fcntl(F_SETFL)");
  return ok();
}

Status set_close_on_exec(Fd fd) {
  const int flags = ::fcntl(fd, F_GETFD, 0);
  if (flags < 0)
    return Error::from_errno("fcntl(F_GETFD)");
  if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
    return Error::from_errno("fcntl(F_SETFD)");
  return ok();
}

Status set_reuse_addr(Fd fd) {
  const int one = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) < 0) {
    return Error::from_errno("setsockopt(SO_REUSEADDR)");
  }
  return ok();
}

Status set_tcp_nodelay(Fd fd) {
  const int one = 1;
  if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) < 0) {
    return Error::from_errno("setsockopt(TCP_NODELAY)");
  }
  return ok();
}

Result<SocketHandle> create_tcp_socket() {
  SocketHandle sock{::socket(AF_INET, SOCK_STREAM, 0)};
  if (!sock)
    return Error::from_errno("socket(AF_INET, SOCK_STREAM)");
  if (Status st = set_non_blocking(sock.get()); !st)
    return st.error();
  if (Status st = set_close_on_exec(sock.get()); !st)
    return st.error();
  return sock;
}

Result<SocketHandle> listen_tcp(std::string_view host, std::uint16_t port, int backlog) {
  auto addr = make_ipv4(host, port);
  if (!addr)
    return addr.error();
  auto sock = create_tcp_socket();
  if (!sock)
    return sock.error();
  if (Status st = set_reuse_addr(sock->get()); !st)
    return st.error();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): required by the BSD API
  if (::bind(sock->get(), reinterpret_cast<const sockaddr*>(&*addr), sizeof(sockaddr_in)) < 0) {
    return Error::from_errno("bind(" + std::string(host) + ":" + std::to_string(port) + ")");
  }
  if (::listen(sock->get(), backlog) < 0)
    return Error::from_errno("listen");
  return std::move(*sock);
}

Result<std::optional<SocketHandle>> accept_tcp(Fd listener) {
  const Fd fd = ::accept(listener, nullptr, nullptr);
  if (fd < 0) {
    if (is_would_block(errno))
      return std::optional<SocketHandle>{};
    if (errno == EINTR || errno == ECONNABORTED)
      return std::optional<SocketHandle>{};
    return Error::from_errno("accept");
  }
  SocketHandle sock{fd};
  if (Status st = set_non_blocking(fd); !st)
    return st.error();
  if (Status st = set_close_on_exec(fd); !st)
    return st.error();
  return std::optional<SocketHandle>{std::move(sock)};
}

Result<std::pair<SocketHandle, ConnectState>> connect_tcp(std::string_view host,
                                                          std::uint16_t port) {
  auto addr = make_ipv4(host, port);
  if (!addr)
    return addr.error();
  auto sock = create_tcp_socket();
  if (!sock)
    return sock.error();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const int rc =
      ::connect(sock->get(), reinterpret_cast<const sockaddr*>(&*addr), sizeof(sockaddr_in));
  if (rc == 0)
    return std::pair{std::move(*sock), ConnectState::Connected};
  if (errno == EINPROGRESS)
    return std::pair{std::move(*sock), ConnectState::InProgress};
  return Error::from_errno("connect(" + std::string(host) + ":" + std::to_string(port) + ")");
}

Result<int> pending_error(Fd fd) {
  int err = 0;
  socklen_t len = sizeof err;
  if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
    return Error::from_errno("getsockopt(SO_ERROR)");
  }
  return err;
}

Result<std::uint16_t> local_port(Fd fd) {
  sockaddr_in addr{};
  socklen_t len = sizeof addr;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
    return Error::from_errno("getsockname");
  }
  return ntohs(addr.sin_port);
}

}  // namespace socket_ops
}  // namespace aegis::net
