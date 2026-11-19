#include "aegisproxy/network/connection.hpp"

#include <cerrno>

#include <sys/socket.h>

#if defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL_COMPAT MSG_NOSIGNAL
#else
// macOS has no MSG_NOSIGNAL; SIGPIPE is ignored process-wide in main.cpp instead.
#define MSG_NOSIGNAL_COMPAT 0
#endif

namespace aegis::net {

Connection::Connection(ConnectionId id, SocketHandle socket, std::size_t buffer_limit_bytes)
    : id_(id),
      socket_(std::move(socket)),
      inbound_(Buffer::kDefaultInitialCapacity, buffer_limit_bytes),
      outbound_(Buffer::kDefaultInitialCapacity, buffer_limit_bytes) {}

Result<std::size_t> Connection::read_available() {
  std::size_t total = 0;
  for (;;) {
    auto region = inbound_.prepare(Buffer::kDefaultInitialCapacity);
    if (!region) {
      if (total > 0)
        break;  // report what we got; caller sees the full buffer next time
      return region.error();
    }
    const ssize_t n = ::recv(socket_.get(), region->data(), region->size(), 0);
    if (n > 0) {
      inbound_.commit(static_cast<std::size_t>(n));
      total += static_cast<std::size_t>(n);
      if (static_cast<std::size_t>(n) < region->size())
        break;  // drained
      continue;
    }
    if (n == 0) {
      if (total > 0)
        break;
      return Error{ErrorCode::ConnectionClosed, "peer closed connection"};
    }
    if (socket_ops::is_would_block(errno))
      break;
    if (errno == EINTR)
      continue;
    return Error::from_errno("recv");
  }
  return total;
}

Result<std::size_t> Connection::flush() {
  std::size_t total = 0;
  while (!outbound_.empty()) {
    const auto data = outbound_.readable();
    const ssize_t n = ::send(socket_.get(), data.data(), data.size(), MSG_NOSIGNAL_COMPAT);
    if (n > 0) {
      outbound_.consume(static_cast<std::size_t>(n));
      total += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && socket_ops::is_would_block(errno))
      break;
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 && errno == EPIPE)
      return Error{ErrorCode::ConnectionClosed, "peer closed (EPIPE)"};
    return Error::from_errno("send");
  }
  return total;
}

}  // namespace aegis::net
