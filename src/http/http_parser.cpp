#include "aegisproxy/http/http_parser.hpp"

namespace aegis::http {

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

Result<HttpParser::Progress> HttpParser::feed(std::string_view input) {
  (void)input;
  if (state_ == State::Failed) {
    return Error{ErrorCode::InvalidState, "parser is in Failed state; call reset()"};
  }
  // TODO(Member 2): implement the state machine described in http_parser.hpp.
  return Error::not_implemented("HttpParser::feed");
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
}

}  // namespace aegis::http
