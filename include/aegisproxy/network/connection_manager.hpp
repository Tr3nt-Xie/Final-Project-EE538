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
  std::size_t max_connections{1024};  ///< client connections; backend sockets are not counted
  std::size_t buffer_limit_bytes{64 * 1024};
};

/// Counters exposed to Member 3 (scheduling statistics) and to the benchmarks.
/// Snapshot semantics; single-threaded.
struct NetworkStats {
  std::size_t open_connections{0};          ///< client + backend
  std::uint64_t total_accepted{0};          ///< client connections accepted
  std::uint64_t total_rejected{0};          ///< accepts dropped by max_connections
  std::uint64_t total_backend_connects{0};  ///< adopt_backend() calls
  std::uint64_t total_closed{0};
  std::uint64_t bytes_in{0};   ///< TODO(Member 1): sum Connection::bytes_read() on close
  std::uint64_t bytes_out{0};  ///< TODO(Member 1): sum Connection::bytes_written() on close
};

/// Owns the listening socket and every Connection (client and backend).
///
/// This is the only object that talks to the EventLoop about sockets. Upper layers
/// (HttpProxy, Member 2) never call EventLoop::add/modify/remove; they use the methods
/// below and receive events through the three handlers:
///
///   data handler      new bytes are in connection.inbound()           (any role)
///   writable handler  outbound() drained, or a backend connect completed (state moved
///                     from ConnectingBackend to ForwardingRequest)
///   close handler     connection is about to be destroyed; last_error() says why
///
/// Flow: listener Readable → accept → adopt() → loop.add(fd, Readable, cb(id))
///       Readable  → read_available() → data handler
///       Writable  → flush(); when drained, drop Writable interest → writable handler
///       Error/HangUp or ConnectionClosed → close(id)
///
/// Closing is two-phase: close(id) unregisters the fd from the loop immediately (no
/// more events can arrive) and defers destruction to after the current dispatch round,
/// so close() is safe to call from inside the connection's own callback.
///
/// Thread-safety: none (event-loop thread only).
class ConnectionManager {
 public:
  using ConnectionHandler = std::function<void(Connection&)>;

  ConnectionManager(EventLoop& loop, ConnectionLimits limits);
  ~ConnectionManager();

  ConnectionManager(const ConnectionManager&) = delete;
  ConnectionManager& operator=(const ConnectionManager&) = delete;

  // --- listening / client side ---------------------------------------------------

  /// Binds, listens and registers the acceptor with the loop. Port 0 = kernel picks.
  Status listen(std::string_view host, std::uint16_t port, int backlog = 128);
  [[nodiscard]] bool is_listening() const noexcept { return listener_.valid(); }
  Result<std::uint16_t> listen_port() const;

  /// Takes ownership of an accepted client socket and registers it for Readable.
  /// Error CapacityExceeded when max_connections is reached (the socket is then closed).
  Result<ConnectionId> adopt(SocketHandle socket);

  // --- backend side (used by Member 2's RequestForwarder) --------------------------

  /// Takes ownership of a socket returned by BackendConnector::begin_connect() and
  /// registers it on behalf of `client_id`. The new connection starts in state
  /// ConnectingBackend with Readable|Writable interest; when the socket becomes
  /// Writable the manager checks SO_ERROR and either (a) moves it to ForwardingRequest
  /// and invokes the writable handler ("connected"), or (b) records last_error() and
  /// closes it (close handler fires — treat as connect failure / failover trigger).
  /// Both connections' peer_id() are linked. Closing the client does NOT close the
  /// backend automatically; the close handler must do that (keeps the policy in proxy/).
  ///
  /// TODO(Member 1): implement; returns NotImplemented in the scaffold.
  Result<ConnectionId> adopt_backend(SocketHandle socket, ConnectionId client_id);

  // --- shared operations -----------------------------------------------------------

  [[nodiscard]] Connection* find(ConnectionId id) noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return connections_.size(); }
  [[nodiscard]] const ConnectionLimits& limits() const noexcept { return limits_; }
  [[nodiscard]] NetworkStats stats() const noexcept;

  /// Enables Writable interest so that outbound() is flushed when the socket allows.
  /// The writable handler fires once the buffer has drained.
  Status request_write(ConnectionId id);

  /// Backpressure mechanism: stop/restart delivering Readable events for `id`
  /// (EventLoop::modify under the hood). The *policy* — when to call these — lives in
  /// BackpressureController and is driven by the proxy layer.
  /// TODO(Member 1): implement; both return NotImplemented in the scaffold.
  Status pause_reads(ConnectionId id);
  Status resume_reads(ConnectionId id);

  /// Unregisters now, destroys after the current dispatch round. Idempotent.
  Status close(ConnectionId id);

  void set_data_handler(ConnectionHandler handler) { data_handler_ = std::move(handler); }
  void set_writable_handler(ConnectionHandler handler) { writable_handler_ = std::move(handler); }
  void set_close_handler(ConnectionHandler handler) { close_handler_ = std::move(handler); }

  [[nodiscard]] std::uint64_t total_accepted() const noexcept { return stats_.total_accepted; }

 private:
  void on_listener_ready(EventMask events);
  void on_connection_ready(ConnectionId id, EventMask events);
  Status set_interest(Connection& conn, EventMask interest);

  EventLoop& loop_;
  ConnectionLimits limits_;
  SocketHandle listener_;
  HandlerId listener_handler_{kInvalidHandlerId};
  std::unordered_map<ConnectionId, std::unique_ptr<Connection>> connections_;
  ConnectionId next_id_{1};
  ConnectionHandler data_handler_;
  ConnectionHandler writable_handler_;
  ConnectionHandler close_handler_;
  NetworkStats stats_;
};

}  // namespace aegis::net
