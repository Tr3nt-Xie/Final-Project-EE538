#include "aegisproxy/proxy/http_proxy.hpp"

#include "aegisproxy/common/log.hpp"
#include "aegisproxy/http/http_parser.hpp"
#include "aegisproxy/http/http_request.hpp"
#include "aegisproxy/http/http_response.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aegis::proxy {

namespace {

constexpr std::string_view kLog = "http_proxy";

/// Per-client-connection proxy state. Owned by the Connection (via set_context), so
/// it dies exactly when the connection does.
///
/// Ownership invariants (docs/ownership.md):
///   * Stores ConnectionId / BackendId only — never raw fds, Connection*, or HandlerIds.
///   * Backend sockets are owned by ConnectionManager (adopt_backend), never here.
///   * Never calls EventLoop::add/modify/remove directly.
class ProxySession final : public net::ConnectionContext {
 public:
  http::HttpParser parser;

  /// Active backend connection id from ConnectionManager::adopt_backend().
  ConnectionId backend_conn{kInvalidConnectionId};
  /// Selected backend id in BackendRegistry.
  BackendId backend_id{kInvalidBackendId};
  /// Guards strict 1:1 pairing between on_connection_started() and on_connection_finished().
  bool backend_accounting_active{false};

  /// Request waiting for an asynchronous backend connect to finish (or for retry).
  std::optional<http::HttpRequest> active_request;
  /// True once at least one byte of active_request has been queued/sent to the backend
  /// (connect-stage failover retry is only allowed while this is false).
  bool request_sent_to_backend{false};
  /// Failover connect attempts made for the current request (max 1-2 retries on connect
  /// failure while request_sent_to_backend is false; passed to SelectionContext skip list).
  unsigned connect_attempts{0};
  /// Backend ids already attempted and failed during connect for the current request.
  std::vector<BackendId> failed_backends;

  /// Timestamp when backend connect began (steady_clock) for latency measurement.
  std::chrono::steady_clock::time_point started_at;
  /// Whether backend response headers have been received (latency = connect -> headers end).
  bool response_headers_received{false};
  /// Measured connect-to-response-headers latency in microseconds.
  /// Convention with Member 3:
  ///   * Success: connect -> response headers (clamped to >= 1us so valid fast samples are kept)
  ///   * BackendFailure: success = false, latency = 0
  ///   * ClientAborted (until CompletionInfo adds an explicit field): success = true, latency = 0
  std::chrono::microseconds response_header_latency{0};

  /// Whether the client connection should be kept alive after the current response.
  bool client_keep_alive{false};

  void reset_exchange() noexcept {
    backend_conn = kInvalidConnectionId;
    backend_id = kInvalidBackendId;
    backend_accounting_active = false;
    active_request.reset();
    request_sent_to_backend = false;
    connect_attempts = 0;
    failed_backends.clear();
    started_at = {};
    response_headers_received = false;
    response_header_latency = std::chrono::microseconds{0};
    client_keep_alive = false;
  }
};

ProxySession& session_for(net::Connection& connection) {
  auto* existing = dynamic_cast<ProxySession*>(connection.context());
  if (existing != nullptr)
    return *existing;
  auto fresh = std::make_unique<ProxySession>();
  ProxySession& ref = *fresh;
  connection.set_context(std::move(fresh));
  return ref;
}

}  // namespace

HttpProxy::HttpProxy(net::ConnectionManager& connections, lb::BackendRegistry& registry,
                     lb::LoadBalancingStrategy& strategy, BackendConnector& connector)
    : connections_(connections), registry_(registry), strategy_(strategy), connector_(connector) {}

void HttpProxy::attach() {
  connections_.set_data_handler([this](net::Connection& c) {
    if (c.role() == net::ConnectionRole::Client) {
      on_client_data(c);
    }
    // TODO(Member 2): else on_backend_data(c) — relay response bytes to the peer client.
  });
  // TODO(Member 2): set_writable_handler (backend connected / client drained) and
  // set_close_handler (report CompletionInfo to registry_, close the peer, or fail over).
}

void HttpProxy::on_client_data(net::Connection& connection) {
  ProxySession& session = session_for(connection);

  auto progress = session.parser.feed(connection.inbound().view());
  if (!progress) {
    if (progress.error().code == ErrorCode::NotImplemented) {
      // Scaffold: fail loudly instead of hanging the client.
      respond_and_close(connection, 501,
                        "AegisProxy scaffold: HTTP parsing is not implemented yet");
      return;
    }
    respond_and_close(connection, 400, progress.error().message);
    return;
  }
  connection.inbound().consume(progress->bytes_consumed);
  if (!progress->complete) {
    return;  // wait for more bytes
  }

  ++requests_seen_;
  auto request = session.parser.take_request();
  if (!request) {
    respond_and_close(connection, 400, request.error().message);
    return;
  }
  session.client_keep_alive = request->keep_alive();

  (void)connection.state_machine().transition_to(net::ConnectionState::SelectingBackend);
  const auto selected = strategy_.select(lb::SelectionContext{"", request->target});
  if (!selected) {
    if (selected.error().code == ErrorCode::NotImplemented) {
      respond_and_close(connection, 501, "load balancing strategy not implemented");
      return;
    }
    if (selected.error().code == ErrorCode::NoHealthyBackend) {
      respond_and_close(connection, 503, "no healthy backend available");
      return;
    }
    respond_and_close(connection, 502, selected.error().message);
    return;
  }

  const lb::BackendEntry* entry = registry_.find(*selected);
  if (entry == nullptr) {
    respond_and_close(connection, 502, "selected backend not found in registry");
    return;
  }

  if (const auto wire = forwarder_.serialize_for_backend(*request); !wire) {
    respond_and_close(connection, 502, "failed to serialize request: " + wire.error().message);
    return;
  }

  session.backend_id = *selected;
  session.active_request = std::move(*request);

  // TODO(Member 2, Week 3): real forwarding pipeline —
  //   auto pending = connector_.begin_connect(entry->endpoint);
  //   auto bid = connections_.adopt_backend(std::move(pending->socket), connection.id());
  //   registry_.on_connection_started(*selected);
  //   forwarder_.forward(connections_, connection.id(), *session.active_request, *bid);
  (void)connector_;
  respond_and_close(connection, 502,
                    "forwarding not implemented (selected backend: " + entry->endpoint.name + ")");
}

void HttpProxy::respond_and_close(net::Connection& connection, int status,
                                  std::string_view detail) {
  const http::HttpResponse resp = http::make_error_response(status, detail);
  const std::string wire = resp.serialize();
  if (Status st = connection.outbound().append(wire); !st) {
    log::warn(kLog, "connection " + std::to_string(connection.id()) +
                        ": cannot queue response: " + st.error().to_string());
    (void)connections_.close(connection.id());
    return;
  }
  (void)connection.state_machine().transition_to(net::ConnectionState::WritingResponse);
  // Try to send immediately; whatever remains is flushed on Writable, then closed.
  (void)connection.flush();
  (void)connection.state_machine().transition_to(net::ConnectionState::Closing);
  if (connection.has_pending_output()) {
    (void)connections_.request_write(connection.id());
  } else {
    (void)connections_.close(connection.id());
  }
}

}  // namespace aegis::proxy
