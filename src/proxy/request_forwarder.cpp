#include "aegisproxy/proxy/request_forwarder.hpp"

namespace aegis::proxy {

Result<std::string> RequestForwarder::serialize_for_backend(
    const http::HttpRequest& request) const {
  (void)request;
  // TODO(Member 2): see header — hop-by-hop header stripping, X-Forwarded-For, etc.
  return Error::not_implemented("RequestForwarder::serialize_for_backend");
}

Status RequestForwarder::forward(net::Connection& client, const http::HttpRequest& request,
                                 Fd backend_fd) {
  (void)client;
  (void)request;
  (void)backend_fd;
  return Error::not_implemented("RequestForwarder::forward");
}

}  // namespace aegis::proxy
