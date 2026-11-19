#include "aegisproxy/network/connection_manager.hpp"

#include "aegisproxy/common/log.hpp"

#include <string>
#include <utility>

namespace aegis::net {

namespace {
constexpr std::string_view kLog = "conn_mgr";
}

ConnectionManager::ConnectionManager(EventLoop& loop, ConnectionLimits limits)
    : loop_(loop), limits_(limits) {}

ConnectionManager::~ConnectionManager() {
  // Unregister everything before the SocketHandles close so the poller never sees a
  // dangling fd; connections_ is destroyed after this body runs.
  for (auto& [id, conn] : connections_) {
    if (conn->handler_id() != kInvalidHandlerId)
      (void)loop_.remove(conn->handler_id());
  }
  if (listener_handler_ != kInvalidHandlerId)
    (void)loop_.remove(listener_handler_);
}

Status ConnectionManager::listen(std::string_view host, std::uint16_t port, int backlog) {
  if (listener_)
    return Error{ErrorCode::InvalidState, "already listening"};
  auto sock = socket_ops::listen_tcp(host, port, backlog);
  if (!sock)
    return sock.error();
  auto id = loop_.add(sock->get(), EventMask::Readable,
                      [this](EventMask events) { on_listener_ready(events); });
  if (!id)
    return id.error();
  listener_ = std::move(*sock);
  listener_handler_ = *id;
  auto bound = listen_port();
  log::info(kLog,
            "listening on " + std::string(host) + ":" + std::to_string(bound ? *bound : port));
  return ok();
}

Result<std::uint16_t> ConnectionManager::listen_port() const {
  if (!listener_)
    return Error{ErrorCode::InvalidState, "not listening"};
  return socket_ops::local_port(listener_.get());
}

void ConnectionManager::on_listener_ready(EventMask events) {
  if (has(events, EventMask::Error)) {
    log::error(kLog, "listener reported an error condition");
    return;
  }
  // Level-triggered: drain the accept queue so we don't wake once per connection.
  for (;;) {
    auto accepted = socket_ops::accept_tcp(listener_.get());
    if (!accepted) {
      log::warn(kLog, "accept failed: " + accepted.error().to_string());
      return;
    }
    if (!accepted->has_value())
      return;  // EAGAIN
    auto id = adopt(std::move(**accepted));
    if (!id) {
      ++stats_.total_rejected;
      log::warn(kLog, "dropping connection: " + id.error().to_string());
    }
  }
}

Result<ConnectionId> ConnectionManager::adopt(SocketHandle socket) {
  if (!socket)
    return Error{ErrorCode::InvalidArgument, "adopt: invalid socket"};
  if (connections_.size() >= limits_.max_connections) {
    return Error{ErrorCode::CapacityExceeded,
                 "connection limit " + std::to_string(limits_.max_connections) + " reached"};
  }
  const ConnectionId id = next_id_++;
  auto conn =
      std::make_unique<Connection>(id, ConnectionRole::Client, std::move(socket),
                                   limits_.buffer_limit_bytes, ConnectionState::ReadingRequest);
  auto handler = loop_.add(conn->fd(), EventMask::Readable,
                           [this, id](EventMask events) { on_connection_ready(id, events); });
  if (!handler)
    return handler.error();
  conn->set_handler_id(*handler);
  conn->set_interest(EventMask::Readable);
  connections_.emplace(id, std::move(conn));
  ++stats_.total_accepted;
  log::debug(kLog, "connection " + std::to_string(id) + " accepted (" +
                       std::to_string(connections_.size()) + " open)");
  return id;
}

Result<ConnectionId> ConnectionManager::adopt_backend(SocketHandle socket, ConnectionId client_id) {
  if (!socket)
    return Error{ErrorCode::InvalidArgument, "adopt_backend: invalid socket"};
  if (find(client_id) == nullptr) {
    return Error{ErrorCode::NotFound, "adopt_backend: unknown client connection"};
  }
  // TODO(Member 1): mirror adopt() with ConnectionRole::Backend and initial state
  // ConnectingBackend; register for Readable | Writable; link peer ids both ways;
  // ++stats_.total_backend_connects. In on_connection_ready(), a Writable event on a
  // connection whose state is ConnectingBackend means the async connect finished:
  // check socket_ops::pending_error(fd) → on 0 transition to ForwardingRequest and call
  // writable_handler_, otherwise set_last_error() and close(). See docs/ownership.md.
  return Error::not_implemented("ConnectionManager::adopt_backend");
}

Connection* ConnectionManager::find(ConnectionId id) noexcept {
  auto it = connections_.find(id);
  return it == connections_.end() ? nullptr : it->second.get();
}

NetworkStats ConnectionManager::stats() const noexcept {
  NetworkStats s = stats_;
  s.open_connections = connections_.size();
  return s;
}

Status ConnectionManager::set_interest(Connection& conn, EventMask interest) {
  if (interest == conn.interest())
    return ok();
  if (Status st = loop_.modify(conn.handler_id(), interest); !st)
    return st;
  conn.set_interest(interest);
  return ok();
}

Status ConnectionManager::request_write(ConnectionId id) {
  Connection* conn = find(id);
  if (conn == nullptr)
    return Error{ErrorCode::NotFound, "request_write: unknown connection"};
  return set_interest(*conn, conn->interest() | EventMask::Writable);
}

Status ConnectionManager::pause_reads(ConnectionId id) {
  if (find(id) == nullptr)
    return Error{ErrorCode::NotFound, "pause_reads: unknown connection"};
  // TODO(Member 1): set_interest(conn, conn.interest() & ~Readable); conn.set_reads_paused(true).
  return Error::not_implemented("ConnectionManager::pause_reads");
}

Status ConnectionManager::resume_reads(ConnectionId id) {
  if (find(id) == nullptr)
    return Error{ErrorCode::NotFound, "resume_reads: unknown connection"};
  // TODO(Member 1): set_interest(conn, conn.interest() | Readable); conn.set_reads_paused(false).
  // Level-triggered epoll re-reports pending bytes immediately, so no manual re-read.
  return Error::not_implemented("ConnectionManager::resume_reads");
}

Status ConnectionManager::close(ConnectionId id) {
  auto it = connections_.find(id);
  if (it == connections_.end())
    return ok();  // already closed: idempotent
  std::unique_ptr<Connection> conn = std::move(it->second);
  connections_.erase(it);
  if (conn->handler_id() != kInvalidHandlerId) {
    (void)loop_.remove(conn->handler_id());
    conn->set_handler_id(kInvalidHandlerId);
  }
  if (conn->state() != ConnectionState::Closing && conn->state() != ConnectionState::Closed) {
    (void)conn->state_machine().transition_to(ConnectionState::Closing);
  }
  (void)conn->state_machine().transition_to(ConnectionState::Closed);
  ++stats_.total_closed;
  log::debug(kLog, "connection " + std::to_string(id) + " closing (" +
                       std::to_string(connections_.size()) + " open)");
  if (close_handler_)
    close_handler_(*conn);
  // Defer destruction: we may be inside this connection's own callback. std::function
  // needs a copyable callable, hence the shared_ptr wrapper around the unique owner.
  loop_.defer(
      [holder = std::shared_ptr<Connection>(std::move(conn))]() mutable { holder.reset(); });
  return ok();
}

void ConnectionManager::on_connection_ready(ConnectionId id, EventMask events) {
  Connection* conn = find(id);
  if (conn == nullptr)
    return;  // closed earlier this round; stale event

  if (has(events, EventMask::Error)) {
    auto err = socket_ops::pending_error(conn->fd());
    conn->set_last_error(err ? Error{ErrorCode::SystemError, "socket error", *err} : err.error());
    log::debug(kLog, "connection " + std::to_string(id) + ": " + conn->last_error()->to_string());
    (void)close(id);
    return;
  }

  if (has(events, EventMask::Readable)) {
    auto n = conn->read_available();
    if (!n) {
      if (n.error().code == ErrorCode::CapacityExceeded) {
        // TODO(Member 1): pause_reads(id) and let the proxy layer drain inbound(); until
        // the backpressure mechanism exists, close so the proxy cannot buffer unboundedly.
        log::warn(kLog, "connection " + std::to_string(id) + ": inbound buffer full, closing");
      } else if (n.error().code != ErrorCode::ConnectionClosed) {
        conn->set_last_error(n.error());
        log::debug(kLog, "connection " + std::to_string(id) + ": " + n.error().to_string());
      }
      (void)close(id);
      return;
    }
    if (*n > 0 && data_handler_) {
      data_handler_(*conn);
      if (find(id) == nullptr)
        return;  // handler closed it
    }
  }

  if (has(events, EventMask::Writable)) {
    // TODO(Member 1): if conn->state() == ConnectingBackend, resolve the async connect
    // here (see adopt_backend) instead of flushing.
    auto n = conn->flush();
    if (!n) {
      conn->set_last_error(n.error());
      (void)close(id);
      return;
    }
    if (!conn->has_pending_output()) {
      (void)set_interest(*conn, conn->interest() & ~EventMask::Writable);
      if (conn->state() == ConnectionState::Closing) {
        (void)close(id);
        return;
      }
      if (writable_handler_) {
        writable_handler_(*conn);
        if (find(id) == nullptr)
          return;
      }
    }
  }

  if (has(events, EventMask::HangUp) && !has(events, EventMask::Readable)) {
    (void)close(id);
  }
}

}  // namespace aegis::net
