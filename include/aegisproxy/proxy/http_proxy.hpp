#pragma once

#include "aegisproxy/balancer/backend_registry.hpp"
#include "aegisproxy/balancer/load_balancing_strategy.hpp"
#include "aegisproxy/common/result.hpp"
#include "aegisproxy/network/connection.hpp"
#include "aegisproxy/network/connection_manager.hpp"
#include "aegisproxy/proxy/backend_connector.hpp"
#include "aegisproxy/proxy/request_forwarder.hpp"

#include <cstdint>
#include <string_view>

namespace aegis::proxy {

/// Orchestrates the request pipeline on top of ConnectionManager:
///
///   client bytes → HttpParser → LoadBalancingStrategy::select → BackendConnector
///                → RequestForwarder → backend → response → client
///
/// HttpProxy does not own the EventLoop or any sockets other than backend sockets it
/// opens per request (owned by the per-connection ProxySession, a ConnectionContext).
/// It is installed via attach(), which sets ConnectionManager's data handler.
///
/// Scaffold behaviour: every request is answered with 501 Not Implemented and the
/// connection is closed, so that an end-to-end run fails loudly rather than hanging.
///
/// Thread-safety: none.
class HttpProxy {
 public:
  HttpProxy(net::ConnectionManager& connections, lb::BackendRegistry& registry,
            lb::LoadBalancingStrategy& strategy, BackendConnector& connector);

  HttpProxy(const HttpProxy&) = delete;
  HttpProxy& operator=(const HttpProxy&) = delete;

  /// Installs the data/close handlers on the ConnectionManager.
  void attach();

  /// Called whenever a client connection has new inbound bytes.
  void on_client_data(net::Connection& connection);

  [[nodiscard]] std::uint64_t requests_seen() const noexcept { return requests_seen_; }

 private:
  void respond_and_close(net::Connection& connection, int status, std::string_view detail);

  net::ConnectionManager& connections_;
  lb::BackendRegistry& registry_;
  lb::LoadBalancingStrategy& strategy_;
  BackendConnector& connector_;
  RequestForwarder forwarder_;
  std::uint64_t requests_seen_{0};
};

}  // namespace aegis::proxy
