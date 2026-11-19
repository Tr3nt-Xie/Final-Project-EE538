#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"
#include "aegisproxy/network/buffer.hpp"
#include "aegisproxy/network/connection_state_machine.hpp"
#include "aegisproxy/network/poller.hpp"
#include "aegisproxy/network/socket_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace aegis::net {

/// Per-connection state that belongs to a higher layer (e.g. HttpProxy's parser and
/// forwarding state). Connection owns it polymorphically so that the network module
/// never depends on http/proxy headers.
class ConnectionContext {
 public:
  virtual ~ConnectionContext() = default;
  ConnectionContext(const ConnectionContext&) = delete;
  ConnectionContext& operator=(const ConnectionContext&) = delete;

 protected:
  ConnectionContext() = default;
};

/// Which side of the proxy a socket belongs to. Backend-role connections are created
/// by ConnectionManager::adopt_backend() on behalf of a client connection (peer_id()).
enum class ConnectionRole : std::uint8_t { Client, Backend };

[[nodiscard]] constexpr std::string_view to_string(ConnectionRole role) noexcept {
  return role == ConnectionRole::Client ? "client" : "backend";
}

/// One socket (client or backend) plus its buffers and lifecycle state.
///
/// Ownership: created and owned by ConnectionManager (unique_ptr). Never held by raw
/// pointer across event-loop rounds — look it up by ConnectionId each time.
/// Neither copyable nor movable: the EventLoop callback captures its id, not `this`.
///
/// Thread-safety: none.
class Connection {
 public:
  Connection(ConnectionId id, ConnectionRole role, SocketHandle socket,
             std::size_t buffer_limit_bytes, ConnectionState initial_state);
  ~Connection() = default;

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;

  [[nodiscard]] ConnectionId id() const noexcept { return id_; }
  [[nodiscard]] ConnectionRole role() const noexcept { return role_; }
  /// For a Backend connection: the client it serves. For a Client: the backend currently
  /// attached, or kInvalidConnectionId. Maintained by ConnectionManager.
  [[nodiscard]] ConnectionId peer_id() const noexcept { return peer_id_; }
  void set_peer_id(ConnectionId id) noexcept { peer_id_ = id; }

  /// Read-only use (e.g. getsockopt). Never close() it; the SocketHandle owns it.
  [[nodiscard]] Fd fd() const noexcept { return socket_.get(); }
  [[nodiscard]] ConnectionState state() const noexcept { return state_machine_.state(); }
  [[nodiscard]] ConnectionStateMachine& state_machine() noexcept { return state_machine_; }

  [[nodiscard]] Buffer& inbound() noexcept { return inbound_; }
  [[nodiscard]] Buffer& outbound() noexcept { return outbound_; }

  /// recv() as much as is available into inbound(). Returns bytes read (0 if the
  /// socket would block). Error ConnectionClosed on EOF, CapacityExceeded if inbound()
  /// is full (the manager then pauses reads — see BackpressureController).
  Result<std::size_t> read_available();

  /// send() as much of outbound() as the socket accepts. Returns bytes written.
  Result<std::size_t> flush();

  [[nodiscard]] bool has_pending_output() const noexcept { return !outbound_.empty(); }
  [[nodiscard]] std::uint64_t bytes_read() const noexcept { return bytes_read_; }
  [[nodiscard]] std::uint64_t bytes_written() const noexcept { return bytes_written_; }

  /// Set by ConnectionManager when the socket reported an error or an async connect
  /// failed; readable from the close handler so upper layers can distinguish "backend
  /// refused" from "client went away".
  [[nodiscard]] const std::optional<Error>& last_error() const noexcept { return last_error_; }
  void set_last_error(Error error) { last_error_ = std::move(error); }

  /// Opaque upper-layer state (see ConnectionContext).
  void set_context(std::unique_ptr<ConnectionContext> ctx) noexcept { context_ = std::move(ctx); }
  [[nodiscard]] ConnectionContext* context() noexcept { return context_.get(); }

  // --- maintained by ConnectionManager; not for upper layers ---
  void set_handler_id(HandlerId id) noexcept { handler_id_ = id; }
  [[nodiscard]] HandlerId handler_id() const noexcept { return handler_id_; }
  [[nodiscard]] EventMask interest() const noexcept { return interest_; }
  void set_interest(EventMask mask) noexcept { interest_ = mask; }
  [[nodiscard]] bool reads_paused() const noexcept { return reads_paused_; }
  void set_reads_paused(bool paused) noexcept { reads_paused_ = paused; }

 private:
  ConnectionId id_;
  ConnectionRole role_;
  ConnectionId peer_id_{kInvalidConnectionId};
  SocketHandle socket_;
  Buffer inbound_;
  Buffer outbound_;
  ConnectionStateMachine state_machine_;
  std::unique_ptr<ConnectionContext> context_;
  std::optional<Error> last_error_;
  HandlerId handler_id_{kInvalidHandlerId};
  EventMask interest_{EventMask::None};
  bool reads_paused_{false};
  std::uint64_t bytes_read_{0};
  std::uint64_t bytes_written_{0};
};

}  // namespace aegis::net
