#include "aegisproxy/proxy/request_forwarder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace aegis::proxy {

namespace {

constexpr std::array<std::string_view, 8> kStaticHopByHopHeaders = {
    "Connection", "Keep-Alive", "Proxy-Authenticate", "Proxy-Authorization",
    "TE",         "Trailers",   "Transfer-Encoding",  "Upgrade",
};

[[nodiscard]] std::string_view trim_ows(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
    s.remove_suffix(1);
  }
  return s;
}

[[nodiscard]] bool has_crlf(std::string_view s) noexcept {
  return s.find('\r') != std::string_view::npos || s.find('\n') != std::string_view::npos;
}

[[nodiscard]] std::vector<std::string_view> collect_connection_tokens(
    const http::HttpRequest& request) {
  std::vector<std::string_view> tokens;
  for (const http::HttpHeader& header : request.headers) {
    if (!http::iequals(header.name, "Connection")) {
      continue;
    }
    std::string_view rest = header.value;
    while (!rest.empty()) {
      const std::size_t comma = rest.find(',');
      const std::string_view part = comma == std::string_view::npos ? rest : rest.substr(0, comma);
      const std::string_view token = trim_ows(part);
      if (!token.empty()) {
        tokens.push_back(token);
      }
      if (comma == std::string_view::npos) {
        break;
      }
      rest.remove_prefix(comma + 1);
    }
  }
  return tokens;
}

[[nodiscard]] bool should_strip_header(std::string_view name,
                                       const std::vector<std::string_view>& connection_tokens) {
  if (http::iequals(name, "Content-Length")) {
    return true;  // Rebuilt from request.body.size() below.
  }
  const auto matches = [name](std::string_view candidate) noexcept {
    return http::iequals(name, candidate);
  };
  return std::ranges::any_of(kStaticHopByHopHeaders, matches) ||
         std::ranges::any_of(connection_tokens, matches);
}

}  // namespace

Result<std::string> RequestForwarder::serialize_for_backend(
    const http::HttpRequest& request) const {
  if (request.target.empty() || has_crlf(request.target)) {
    return Error{ErrorCode::InvalidArgument, "serialize_for_backend: invalid request target"};
  }

  const std::vector<std::string_view> connection_tokens = collect_connection_tokens(request);

  std::string out;
  out.reserve(128 + request.target.size() + request.body.size());
  out += http::to_string(request.method);
  out += ' ';
  out += request.target;
  out += " HTTP/1.1\r\n";

  for (const http::HttpHeader& header : request.headers) {
    if (header.name.empty() || has_crlf(header.name) || has_crlf(header.value)) {
      return Error{ErrorCode::InvalidArgument, "serialize_for_backend: invalid header field"};
    }
    if (should_strip_header(header.name, connection_tokens)) {
      continue;
    }
    out += header.name;
    out += ": ";
    out += header.value;
    out += "\r\n";
  }

  // TODO(Member 2, Week 5): append X-Forwarded-For / Via once client peer address is plumbed
  // through, and switch to backend keep-alive once response parsing lands.
  out += "Connection: close\r\n";

  if (!request.body.empty() || request.header("Content-Length").has_value()) {
    out += "Content-Length: ";
    out += std::to_string(request.body.size());
    out += "\r\n";
  }

  out += "\r\n";
  out += request.body;
  return out;
}

Status RequestForwarder::forward(net::ConnectionManager& connections, ConnectionId client,
                                 const http::HttpRequest& request, ConnectionId backend) {
  (void)connections;
  (void)client;
  (void)request;
  (void)backend;
  return Error::not_implemented("RequestForwarder::forward");
}

}  // namespace aegis::proxy
