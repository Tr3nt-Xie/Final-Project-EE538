#include "aegisproxy/http/http_parser.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aegis::http {

namespace {

constexpr std::string_view kCrlf = "\r\n";

/// RFC 9110 §5.6.2 token character (tchar).
[[nodiscard]] bool is_tchar(char c) noexcept {
  const auto uc = static_cast<unsigned char>(c);
  if ((uc >= '0' && uc <= '9') || (uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z')) {
    return true;
  }
  switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
      return true;
    default:
      return false;
  }
}

/// RFC 9110 §5.5 field value character: visible ASCII, SP, HTAB, or obs-text (>= 0x80).
[[nodiscard]] bool is_valid_field_char(char c) noexcept {
  const auto uc = static_cast<unsigned char>(c);
  return uc == '\t' || (uc >= 0x20U && uc <= 0x7EU) || uc >= 0x80U;
}

[[nodiscard]] bool is_valid_target_char(char c) noexcept {
  const auto uc = static_cast<unsigned char>(c);
  return uc > 0x20U && uc != 0x7FU;
}

[[nodiscard]] std::string_view trim_ows(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
    s.remove_suffix(1);
  }
  return s;
}

[[nodiscard]] std::optional<std::uint64_t> parse_strict_uint64(std::string_view raw) noexcept {
  if (raw.empty()) {
    return std::nullopt;
  }
  for (const char c : raw) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
  }
  std::uint64_t value = 0;
  const auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (ec != std::errc{} || ptr != raw.data() + raw.size()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::string_view to_string(HttpParser::State state) noexcept {
  switch (state) {
    case HttpParser::State::RequestLine:
      return "RequestLine";
    case HttpParser::State::Headers:
      return "Headers";
    case HttpParser::State::Body:
      return "Body";
    case HttpParser::State::Complete:
      return "Complete";
    case HttpParser::State::Failed:
      return "Failed";
  }
  return "?";
}

Error HttpParser::fail(std::string message) {
  state_ = State::Failed;
  return Error{ErrorCode::ParseError, std::move(message)};
}

Status HttpParser::parse_request_line(std::string_view line) {
  if (line.empty()) {
    return fail("empty request line");
  }
  if (line.find('\r') != std::string_view::npos || line.find('\n') != std::string_view::npos) {
    return fail("request line contains bare CR or LF");
  }

  const std::size_t sp1 = line.find(' ');
  if (sp1 == std::string_view::npos || sp1 == 0) {
    return fail("malformed request line: missing method or target");
  }
  const std::size_t sp2 = line.find(' ', sp1 + 1);
  if (sp2 == std::string_view::npos || sp2 == sp1 + 1) {
    return fail("malformed request line: missing target or HTTP version");
  }
  if (line.find(' ', sp2 + 1) != std::string_view::npos) {
    return fail("malformed request line: unexpected extra spaces");
  }

  const std::string_view method_sv = line.substr(0, sp1);
  const std::string_view target_sv = line.substr(sp1 + 1, sp2 - sp1 - 1);
  const std::string_view version_sv = line.substr(sp2 + 1);

  const auto method = method_from_string(method_sv);
  if (!method) {
    return fail("unsupported or malformed HTTP method");
  }
  for (const char c : target_sv) {
    if (!is_valid_target_char(c)) {
      return fail("request target contains invalid character");
    }
  }
  const auto version = version_from_string(version_sv);
  if (!version) {
    return fail("unsupported HTTP version");
  }

  request_.method = *method;
  request_.target.assign(target_sv);
  request_.version = *version;
  return ok();
}

Status HttpParser::parse_header_line(std::string_view line) {
  if (line.find('\r') != std::string_view::npos || line.find('\n') != std::string_view::npos) {
    return fail("header line contains bare CR or LF");
  }
  const std::size_t colon = line.find(':');
  if (colon == std::string_view::npos || colon == 0) {
    return fail("malformed header line: missing colon or header name");
  }
  const std::string_view name = line.substr(0, colon);
  for (const char c : name) {
    if (!is_tchar(c)) {
      return fail("invalid character in header name");
    }
  }
  const std::string_view value = trim_ows(line.substr(colon + 1));
  for (const char c : value) {
    if (!is_valid_field_char(c)) {
      return fail("invalid character in header value");
    }
  }

  if (request_.headers.size() >= kMaxHeaderCount) {
    return fail("header count exceeds kMaxHeaderCount");
  }
  request_.headers.push_back(HttpHeader{std::string(name), std::string(value)});
  return ok();
}

Status HttpParser::finish_headers() {
  bool has_transfer_encoding = false;
  bool is_chunked = false;
  std::optional<std::uint64_t> content_length;

  for (const HttpHeader& header : request_.headers) {
    if (iequals(header.name, "Transfer-Encoding")) {
      has_transfer_encoding = true;
      if (iequals(header.value, "chunked")) {
        is_chunked = true;
      } else {
        return fail("unsupported Transfer-Encoding");
      }
    } else if (iequals(header.name, "Content-Length")) {
      const auto parsed = parse_strict_uint64(header.value);
      if (!parsed) {
        return fail("invalid Content-Length header");
      }
      if (content_length && *content_length != *parsed) {
        return fail("conflicting Content-Length headers");
      }
      content_length = parsed;
    }
  }

  if (has_transfer_encoding && content_length) {
    return fail("conflicting Transfer-Encoding and Content-Length headers");
  }

  if (is_chunked) {
    chunked_ = true;
    state_ = State::Body;
    return ok();
  }

  if (content_length && *content_length > 0) {
    if (*content_length > kMaxBodyBytes) {
      return fail("request body exceeds kMaxBodyBytes");
    }
    body_remaining_ = *content_length;
    request_.body.reserve(
        std::min<std::size_t>(static_cast<std::size_t>(body_remaining_), kMaxHeaderBytes));
    state_ = State::Body;
    return ok();
  }

  state_ = State::Complete;
  return ok();
}

Result<HttpParser::StepOutcome> HttpParser::step_request_line(std::string_view input,
                                                              std::size_t& consumed) {
  const std::string_view remaining = input.substr(consumed);
  const std::size_t crlf = remaining.find(kCrlf);
  if (crlf == std::string_view::npos) {
    if (remaining.find('\n') != std::string_view::npos) {
      return fail("malformed request line: bare LF");
    }
    if (remaining.size() > kMaxRequestLineBytes) {
      return fail("request line exceeds kMaxRequestLineBytes");
    }
    return StepOutcome::NeedMore;
  }
  if (crlf > kMaxRequestLineBytes) {
    return fail("request line exceeds kMaxRequestLineBytes");
  }
  if (const Status st = parse_request_line(remaining.substr(0, crlf)); !st) {
    return st.error();
  }
  consumed += crlf + kCrlf.size();
  state_ = State::Headers;
  return StepOutcome::Continue;
}

Result<HttpParser::StepOutcome> HttpParser::step_headers(std::string_view input,
                                                         std::size_t& consumed) {
  const std::string_view remaining = input.substr(consumed);
  const std::size_t crlf = remaining.find(kCrlf);
  if (crlf == std::string_view::npos) {
    if (remaining.find('\n') != std::string_view::npos) {
      return fail("malformed header: bare LF");
    }
    if (remaining.size() > kMaxHeaderBytes || header_bytes_ + remaining.size() > kMaxHeaderBytes) {
      return fail("headers exceed kMaxHeaderBytes");
    }
    return StepOutcome::NeedMore;
  }

  const std::size_t line_bytes = crlf + kCrlf.size();
  if (line_bytes > kMaxHeaderBytes || header_bytes_ + line_bytes > kMaxHeaderBytes) {
    return fail("headers exceed kMaxHeaderBytes");
  }
  header_bytes_ += line_bytes;

  const std::string_view line = remaining.substr(0, crlf);
  consumed += line_bytes;

  if (line.empty()) {
    if (const Status st = finish_headers(); !st) {
      return st.error();
    }
    return state_ == State::Complete ? StepOutcome::Complete : StepOutcome::Continue;
  }

  if (const Status st = parse_header_line(line); !st) {
    return st.error();
  }
  return StepOutcome::Continue;
}

Result<HttpParser::StepOutcome> HttpParser::parse_body_fixed(std::string_view input,
                                                             std::size_t& consumed) {
  if (body_remaining_ == 0) {
    state_ = State::Complete;
    return StepOutcome::Complete;
  }
  const std::string_view remaining = input.substr(consumed);
  if (remaining.empty()) {
    return StepOutcome::NeedMore;
  }
  const std::size_t to_take =
      static_cast<std::size_t>(std::min<std::uint64_t>(body_remaining_, remaining.size()));
  request_.body.append(remaining.data(), to_take);
  consumed += to_take;
  body_remaining_ -= to_take;
  if (body_remaining_ == 0) {
    state_ = State::Complete;
    return StepOutcome::Complete;
  }
  return StepOutcome::NeedMore;
}

Result<HttpParser::StepOutcome> HttpParser::step_body(std::string_view input,
                                                      std::size_t& consumed) {
  // TODO(Member 2): implement parse_body_chunked() (Week 4).
  if (chunked_) {
    return Error::not_implemented("HttpParser chunked body");
  }
  return parse_body_fixed(input, consumed);
}

Result<HttpParser::Progress> HttpParser::feed(std::string_view input) {
  if (state_ == State::Failed) {
    return Error{ErrorCode::InvalidState, "parser is in Failed state; call reset()"};
  }
  if (state_ == State::Complete) {
    return Error{ErrorCode::InvalidState,
                 "request already complete; call take_request() or reset()"};
  }

  std::size_t consumed = 0;

  while (true) {
    Result<StepOutcome> step = StepOutcome::NeedMore;
    switch (state_) {
      case State::RequestLine:
        step = step_request_line(input, consumed);
        break;
      case State::Headers:
        step = step_headers(input, consumed);
        break;
      case State::Body:
        step = step_body(input, consumed);
        break;
      case State::Complete:
        return Progress{consumed, true};
      case State::Failed:
        return Error{ErrorCode::InvalidState, "parser is in Failed state; call reset()"};
    }

    if (!step) {
      return step.error();
    }
    if (*step == StepOutcome::NeedMore) {
      return Progress{consumed, false};
    }
    if (*step == StepOutcome::Complete) {
      return Progress{consumed, true};
    }
  }
}

Result<HttpRequest> HttpParser::take_request() {
  if (state_ != State::Complete) {
    return Error{ErrorCode::InvalidState, "take_request: no complete request available"};
  }
  HttpRequest out = std::move(request_);
  reset();
  return out;
}

void HttpParser::reset() noexcept {
  state_ = State::RequestLine;
  request_ = HttpRequest{};
  body_remaining_ = 0;
  header_bytes_ = 0;
  chunked_ = false;
}

}  // namespace aegis::http
