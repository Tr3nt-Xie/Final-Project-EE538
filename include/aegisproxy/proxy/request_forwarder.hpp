#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"
#include "aegisproxy/http/http_request.hpp"
#include "aegisproxy/network/connection_manager.hpp"

namespace aegis::proxy {

/// Moves one request from a client connection to a backend connection and streams the
/// response back. Both connections are owned by ConnectionManager; the forwarder only
/// touches their Buffers and calls request_write()/pause_reads()/resume_reads()/close().
///
/// TODO(Member 2):
///   * serialize_for_backend(): rebuild the request line + headers for the backend
///     (strip hop-by-hop headers, add X-Forwarded-For, force Connection semantics we
///     support);
///   * relay client→backend body bytes and backend→client response bytes through the
///     Connection buffers, enabling Writable interest via ConnectionManager::
///     request_write() and honouring BackpressureController decisions;
///   * report lb::BackendRegistry::on_connection_started/finished with latency;
///   * on backend connect failure (close handler with last_error()), retry on another
///     backend (automatic failover) and fall back to a 502 after N attempts.
class RequestForwarder {
 public:
  RequestForwarder() = default;

  /// Writes a backend-facing serialisation of `request` into `out`.
  Result<std::string> serialize_for_backend(const http::HttpRequest& request) const;

  /// Begins forwarding `request` from `client` to the backend connection `backend`
  /// (both ids from `connections`). Returns NotImplemented in the scaffold.
  Status forward(net::ConnectionManager& connections, ConnectionId client,
                 const http::HttpRequest& request, ConnectionId backend);
};

}  // namespace aegis::proxy
