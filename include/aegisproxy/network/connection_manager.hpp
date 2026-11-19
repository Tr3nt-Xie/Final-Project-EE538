#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"
#include "aegisproxy/network/connection.hpp"
#include "aegisproxy/network/event_loop.hpp"
#include "aegisproxy/network/socket_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace aegis::net {

struct ConnectionLimits {
  std::size_t max_connections{1024};
  std::size_t buffer_limit_bytes{64 * 1024};
};

/// Owns the listening socket and every client Connection.
///
/// Flow: listener Readable → accept → adopt() → loop.add(fd, Readable, cb(id))
///       client Readable  → read_available() → data_handler(conn)
///       client Writable  → flush(); when drained, drop Writable interest
///       Error/HangUp or ConnectionClosed → close(id)
///
/// Closing is two-phase: close(id) unregisters the fd from the loop immediately (no
/// more events can arrive) and defers destruction to after the current dispatch round,
/// so close() is safe to call from inside the connection's own callback.
///
/// Upper layers (HttpProxy) attach via set_data_handler(); the manager never parses
/// bytes itself.
///
/// Thread-safety: none (event-loop thread only).
class ConnectionManager {
 public:
  using DataHandler = std::function<void(Connection&)>;
  using CloseHandler = std::function<void(ConnectionId)>;

  ConnectionManager(EventLoop& loop, ConnectionLimits limits);
  ~ConnectionManager();

  ConnectionManager(const ConnectionManager&) = delete;
  ConnectionManager& operator=(const ConnectionManager&) = delete;

  /// Binds, listens and registers the acceptor with the loop. Port 0 = kernel picks.
  Status listen(std::string_view host, std::uint16_t port, int backlog = 128);
  [[nodiscard]] bool is_listening() const noexcept { return listener_.valid(); }
  /// Bound port (valid after listen()).
  Result<std::uint16_t> listen_port() const;

  /// Takes ownership of an already-connected socket and registers it. Error
  /// CapacityExceeded when max_connections is reached (the socket is then closed).
  Result<ConnectionId> adopt(SocketHandle socket);

  [[nodiscard]] Connection* find(ConnectionId id) noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return connections_.size(); }
  [[nodiscard]] const ConnectionLimits& limits() const noexcept { return limits_; }

  /// Enables Writable interest so that outbound() is flushed when the socket allows.
  Status request_write(ConnectionId id);

  /// Unregisters now, destroys after the current dispatch round. Idempotent.
  Status close(ConnectionId id);

  void set_data_handler(DataHandler handler) { data_handler_ = std::move(handler); }
  void set_close_handler(CloseHandler handler) { close_handler_ = std::move(handler); }

  [[nodiscard]] std::uint64_t total_accepted() const noexcept { return total_accepted_; }

 private:
  void on_listener_ready(EventMask events);
  void on_connection_ready(ConnectionId id, EventMask events);

  EventLoop& loop_;
  ConnectionLimits limits_;
  SocketHandle listener_;
  HandlerId listener_handler_{kInvalidHandlerId};
  std::unordered_map<ConnectionId, std::unique_ptr<Connection>> connections_;
  ConnectionId next_id_{1};
  DataHandler data_handler_;
  CloseHandler close_handler_;
  std::uint64_t total_accepted_{0};
};

}  // namespace aegis::net
