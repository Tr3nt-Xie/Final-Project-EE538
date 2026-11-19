#include "aegisproxy/proxy/http_proxy.hpp"

#include "aegisproxy/common/log.hpp"
#include "aegisproxy/http/http_parser.hpp"
#include "aegisproxy/http/http_response.hpp"

#include <memory>
#include <string>

namespace aegis::proxy {

namespace {

constexpr std::string_view kLog = "http_proxy";

/// Per-client-connection proxy state. Owned by the Connection (via set_context), so
/// it dies exactly when the connection does.
class ProxySession final : public net::ConnectionContext {
 public:
  http::HttpParser parser;
  // TODO(Member 2): net::SocketHandle backend; HandlerId backend_handler; BackendId
  // backend_id; std::chrono::steady_clock::time_point started_at; for forwarding.
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
  connections_.set_data_handler([this](net::Connection& c) { on_client_data(c); });
}

void HttpProxy::on_client_data(net::Connection& connection) {
  ProxySession& session = session_for(connection);
  ++requests_seen_;

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
  if (!progress->complete)
    return;  // wait for more bytes

  // TODO(Member 2): the real pipeline —
  //   auto request = session.parser.take_request();
  //   auto backend = strategy_.select({...});            // NoHealthyBackend → 503
  //   auto pending = connector_.begin_connect(endpoint);  // failure → failover/502
  //   registry_.on_connection_started(backend);
  //   forwarder_.forward(connection, request, pending.socket.get());
  (void)registry_;
  (void)strategy_;
  (void)connector_;
  (void)forwarder_;
  respond_and_close(connection, 501, "forwarding not implemented");
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
