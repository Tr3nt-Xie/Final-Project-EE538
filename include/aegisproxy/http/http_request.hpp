#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aegis::http {

enum class HttpMethod : std::uint8_t {
  Get,
  Head,
  Post,
  Put,
  Delete,
  Options,
  Patch,
  Connect,
  Trace,
};

enum class HttpVersion : std::uint8_t { Http10, Http11 };

[[nodiscard]] std::string_view to_string(HttpMethod method) noexcept;
[[nodiscard]] std::string_view to_string(HttpVersion version) noexcept;
[[nodiscard]] std::optional<HttpMethod> method_from_string(std::string_view token) noexcept;
[[nodiscard]] std::optional<HttpVersion> version_from_string(std::string_view token) noexcept;

/// Header field. Names are compared case-insensitively (RFC 9110 §5.1).
struct HttpHeader {
  std::string name;
  std::string value;
};

/// Case-insensitive ASCII comparison used for header names.
[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept;

/// Parsed HTTP/1.x request. Produced by HttpParser; consumed by HttpProxy.
/// Plain data: copyable, movable, no invariants beyond "what the parser put in".
struct HttpRequest {
  HttpMethod method{HttpMethod::Get};
  std::string target;  ///< request-target as received, e.g. "/api/v1/items?x=1"
  HttpVersion version{HttpVersion::Http11};
  std::vector<HttpHeader> headers;
  std::string body;

  /// First header with that name (case-insensitive), if any.
  [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const noexcept;

  /// Content-Length as a number, if present and well-formed.
  [[nodiscard]] std::optional<std::uint64_t> content_length() const noexcept;

  /// HTTP/1.1 defaults to keep-alive; HTTP/1.0 defaults to close.
  [[nodiscard]] bool keep_alive() const noexcept;
};

}  // namespace aegis::http
