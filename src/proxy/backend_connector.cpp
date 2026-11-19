#include "aegisproxy/proxy/backend_connector.hpp"

#include <cstring>
#include <string>

namespace aegis::proxy {

Result<BackendConnector::Pending> BackendConnector::begin_connect(
    const lb::BackendEndpoint& endpoint) const {
  auto r = net::socket_ops::connect_tcp(endpoint.host, endpoint.port);
  if (!r)
    return r.error();
  return Pending{std::move(r->first), r->second};
}

Status BackendConnector::finish_connect(Fd backend_fd) const {
  auto err = net::socket_ops::pending_error(backend_fd);
  if (!err)
    return err.error();
  if (*err != 0) {
    return Error{ErrorCode::SystemError, std::string("async connect failed"), *err};
  }
  return ok();
}

}  // namespace aegis::proxy
