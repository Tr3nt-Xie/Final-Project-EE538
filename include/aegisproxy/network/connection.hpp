#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"
#include "aegisproxy/network/buffer.hpp"
#include "aegisproxy/network/connection_state_machine.hpp"
#include "aegisproxy/network/socket_handle.hpp"

#include <cstddef>
#include <memory>

namespace aegis::net {

/// Per-connection state that belongs to a higher layer (e.g. HttpProxy's parser and
/// backend link). Connection owns it polymorphically so that the network module never
/// depends on http/proxy headers.
class ConnectionContext {
 public:
  virtual ~ConnectionContext() = default;
  ConnectionContext(const ConnectionContext&) = delete;
  ConnectionContext& operator=(const ConnectionContext&) = delete;

 protected:
  ConnectionContext() = default;
};

/// One accepted client socket plus its buffers and lifecycle state.
///
/// Ownership: created and owned by ConnectionManager (unique_ptr). Never held by raw
/// pointer across event-loop rounds — look it up by ConnectionId each time.
/// Neither copyable nor movable: the EventLoop callback captures its id, not `this`.
///
/// Thread-safety: none.
class Connection {
 public:
  Connection(ConnectionId id, SocketHandle socket, std::size_t buffer_limit_bytes);
  ~Connection() = default;

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;

  [[nodiscard]] ConnectionId id() const noexcept { return id_; }
  [[nodiscard]] Fd fd() const noexcept { return socket_.get(); }
  [[nodiscard]] ConnectionState state() const noexcept { return state_machine_.state(); }
  [[nodiscard]] ConnectionStateMachine& state_machine() noexcept { return state_machine_; }

  [[nodiscard]] Buffer& inbound() noexcept { return inbound_; }
  [[nodiscard]] Buffer& outbound() noexcept { return outbound_; }

  /// recv() as much as is available into inbound(). Returns bytes read (0 if the
  /// socket would block). Error ConnectionClosed on EOF, CapacityExceeded if inbound()
  /// is full (caller should stop reading — see BackpressureController).
  Result<std::size_t> read_available();

  /// send() as much of outbound() as the socket accepts. Returns bytes written.
  Result<std::size_t> flush();

  [[nodiscard]] bool has_pending_output() const noexcept { return !outbound_.empty(); }

  /// Opaque upper-layer state (see ConnectionContext).
  void set_context(std::unique_ptr<ConnectionContext> ctx) noexcept { context_ = std::move(ctx); }
  [[nodiscard]] ConnectionContext* context() noexcept { return context_.get(); }

  /// Set by ConnectionManager; the loop registration this connection corresponds to.
  void set_handler_id(HandlerId id) noexcept { handler_id_ = id; }
  [[nodiscard]] HandlerId handler_id() const noexcept { return handler_id_; }

 private:
  ConnectionId id_;
  SocketHandle socket_;
  Buffer inbound_;
  Buffer outbound_;
  ConnectionStateMachine state_machine_;
  std::unique_ptr<ConnectionContext> context_;
  HandlerId handler_id_{kInvalidHandlerId};
};

}  // namespace aegis::net
