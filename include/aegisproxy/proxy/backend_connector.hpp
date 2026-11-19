#pragma once

#include "aegisproxy/balancer/backend.hpp"
#include "aegisproxy/common/result.hpp"
#include "aegisproxy/network/socket_handle.hpp"

#include <cstdint>
#include <string_view>

namespace aegis::proxy {

/// Opens non-blocking TCP connections to backends.
///
/// Ownership: the returned SocketHandle is owned by the caller (RequestForwarder /
/// the ProxySession), which registers it with the EventLoop and closes it when the
/// exchange finishes. BackendConnector itself is stateless.
///
/// TODO(Member 2): connection pooling / keep-alive to backends, connect timeout
/// (needs EventLoop timers), and hostname resolution at config-load time.
class BackendConnector {
 public:
  struct Pending {
    net::SocketHandle socket;
    net::socket_ops::ConnectState state;  ///< Connected or InProgress
  };

  BackendConnector() = default;

  /// Starts connecting. On InProgress, wait for Writable then call finish().
  Result<Pending> begin_connect(const lb::BackendEndpoint& endpoint) const;

  /// Resolves an InProgress connect once the socket is Writable: Ok when connected,
  /// SystemError (with errno) otherwise.
  Status finish_connect(Fd backend_fd) const;
};

}  // namespace aegis::proxy
