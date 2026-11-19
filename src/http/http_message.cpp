#include "aegisproxy/http/http_request.hpp"
#include "aegisproxy/http/http_response.hpp"

#include <cctype>
#include <charconv>
#include <string>

namespace aegis::http {

std::string_view to_string(HttpMethod method) noexcept {
  switch (method) {
    case HttpMethod::Get:
      return "GET";
    case HttpMethod::Head:
      return "HEAD";
    case HttpMethod::Post:
      return "POST";
    case HttpMethod::Put:
      return "PUT";
    case HttpMethod::Delete:
      return "DELETE";
    case HttpMethod::Options:
      return "OPTIONS";
    case HttpMethod::Patch:
      return "PATCH";
    case HttpMethod::Connect:
      return "CONNECT";
    case HttpMethod::Trace:
      return "TRACE";
  }
  return "?";
}

std::string_view to_string(HttpVersion version) noexcept {
  switch (version) {
    case HttpVersion::Http10:
      return "HTTP/1.0";
    case HttpVersion::Http11:
      return "HTTP/1.1";
  }
  return "?";
}

std::optional<HttpMethod> method_from_string(std::string_view token) noexcept {
  // Method tokens are case-sensitive (RFC 9110 §9.1).
  if (token == "GET")
    return HttpMethod::Get;
  if (token == "HEAD")
    return HttpMethod::Head;
  if (token == "POST")
    return HttpMethod::Post;
  if (token == "PUT")
    return HttpMethod::Put;
  if (token == "DELETE")
    return HttpMethod::Delete;
  if (token == "OPTIONS")
    return HttpMethod::Options;
  if (token == "PATCH")
    return HttpMethod::Patch;
  if (token == "CONNECT")
    return HttpMethod::Connect;
  if (token == "TRACE")
    return HttpMethod::Trace;
  return std::nullopt;
}

std::optional<HttpVersion> version_from_string(std::string_view token) noexcept {
  if (token == "HTTP/1.1")
    return HttpVersion::Http11;
  if (token == "HTTP/1.0")
    return HttpVersion::Http10;
  return std::nullopt;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const auto ca = static_cast<unsigned char>(a[i]);
    const auto cb = static_cast<unsigned char>(b[i]);
    if (std::tolower(ca) != std::tolower(cb))
      return false;
  }
  return true;
}

namespace {
std::optional<std::string_view> find_header(const std::vector<HttpHeader>& headers,
                                            std::string_view name) noexcept {
  for (const HttpHeader& h : headers) {
    if (iequals(h.name, name))
      return std::string_view{h.value};
  }
  return std::nullopt;
}
}  // namespace

std::optional<std::string_view> HttpRequest::header(std::string_view name) const noexcept {
  return find_header(headers, name);
}

std::optional<std::uint64_t> HttpRequest::content_length() const noexcept {
  const auto raw = header("Content-Length");
  if (!raw)
    return std::nullopt;
  std::uint64_t value = 0;
  const auto [ptr, ec] = std::from_chars(raw->data(), raw->data() + raw->size(), value);
  if (ec != std::errc{} || ptr != raw->data() + raw->size())
    return std::nullopt;
  return value;
}

bool HttpRequest::keep_alive() const noexcept {
  const auto conn = header("Connection");
  if (version == HttpVersion::Http11) {
    return !(conn && iequals(*conn, "close"));
  }
  return conn && iequals(*conn, "keep-alive");
}

std::optional<std::string_view> HttpResponse::header(std::string_view name) const noexcept {
  return find_header(headers, name);
}

std::string HttpResponse::serialize() const {
  std::string out;
  out.reserve(128 + body.size());
  out += to_string(version);
  out += ' ';
  out += std::to_string(status_code);
  out += ' ';
  out += reason.empty() ? std::string(reason_phrase(status_code)) : reason;
  out += "\r\n";
  bool has_length = false;
  for (const HttpHeader& h : headers) {
    if (iequals(h.name, "Content-Length"))
      has_length = true;
    out += h.name;
    out += ": ";
    out += h.value;
    out += "\r\n";
  }
  if (!has_length && !header("Transfer-Encoding")) {
    out += "Content-Length: ";
    out += std::to_string(body.size());
    out += "\r\n";
  }
  out += "\r\n";
  out += body;
  return out;
}

std::string_view reason_phrase(int status_code) noexcept {
  switch (status_code) {
    case 200:
      return "OK";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 408:
      return "Request Timeout";
    case 413:
      return "Content Too Large";
    case 431:
      return "Request Header Fields Too Large";
    case 500:
      return "Internal Server Error";
    case 501:
      return "Not Implemented";
    case 502:
      return "Bad Gateway";
    case 503:
      return "Service Unavailable";
    case 504:
      return "Gateway Timeout";
    default:
      return "Unknown";
  }
}

HttpResponse make_error_response(int status_code, std::string_view detail) {
  HttpResponse resp;
  resp.status_code = status_code;
  resp.reason = std::string(reason_phrase(status_code));
  resp.headers = {{"Content-Type", "text/plain; charset=utf-8"},
                  {"Connection", "close"},
                  {"Server", "AegisProxy/0.1"}};
  resp.body = std::to_string(status_code) + " " + resp.reason + "\n";
  if (!detail.empty()) {
    resp.body += detail;
    resp.body += '\n';
  }
  return resp;
}

}  // namespace aegis::http
