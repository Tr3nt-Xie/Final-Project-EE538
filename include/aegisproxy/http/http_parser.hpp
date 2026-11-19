#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/http/http_request.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace aegis::http {

/// Incremental HTTP/1.1 request parser.
///
/// Usage from HttpProxy:
///   auto progress = parser.feed(connection.inbound().view());
///   connection.inbound().consume(progress->bytes_consumed);
///   if (progress->complete) { HttpRequest req = parser.take_request(); parser.reset(); }
///
/// The parser never blocks and never owns the buffer; it only advances its state
/// machine over whatever bytes are available. Partial input is normal.
///
/// OWNER: Member 2. feed() returns NotImplemented until written. Implementation plan:
///   1. RequestLine: find CRLF; split "METHOD SP target SP HTTP/x.y"; reject anything
///      that is not HTTP/1.0 or HTTP/1.1 → ParseError.
///   2. Headers: loop over "name: value" lines until the empty line; enforce a max
///      header count and total size (kMaxHeaderBytes) → ParseError when exceeded.
///   3. Body: Content-Length (copy exactly N bytes) or Transfer-Encoding: chunked
///      (chunk-size line, data, CRLF, ..., 0-size chunk, optional trailers). Both must
///      be supported for correct forwarding; chunked may land in a later milestone.
///   4. Complete: expose the request via take_request(); reset() for keep-alive.
/// Limits are deliberately part of the parser so a malicious client cannot make the
/// proxy buffer an unbounded header block.
class HttpParser {
 public:
  enum class State : std::uint8_t { RequestLine, Headers, Body, Complete, Failed };

  struct Progress {
    std::size_t bytes_consumed{0};  ///< how many input bytes the caller may discard
    bool complete{false};           ///< a full request is ready (take_request())
  };

  static constexpr std::size_t kMaxRequestLineBytes = 8 * 1024;
  static constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
  static constexpr std::size_t kMaxHeaderCount = 100;

  HttpParser() = default;

  /// Consumes as much of `input` as possible. ParseError leaves the parser in Failed
  /// state until reset().
  Result<Progress> feed(std::string_view input);

  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool is_complete() const noexcept { return state_ == State::Complete; }

  /// Moves the parsed request out. Only valid when is_complete().
  Result<HttpRequest> take_request();

  /// Returns to RequestLine with an empty request (keep-alive reuse).
  void reset() noexcept;

 private:
  // TODO(Member 2): private helpers — parse_request_line(), parse_header_line(),
  // parse_body_fixed(), parse_body_chunked().
  State state_{State::RequestLine};
  HttpRequest request_;
  std::uint64_t body_remaining_{0};
  std::size_t header_bytes_{0};
};

[[nodiscard]] std::string_view to_string(HttpParser::State state) noexcept;

}  // namespace aegis::http
