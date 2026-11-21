#include "aegisproxy/http/http_parser.hpp"
#include "aegisproxy/http/http_response.hpp"

#include <gtest/gtest.h>

namespace aegis::http {

// ---- Implemented now: message helpers ----

TEST(HttpMethod, RoundTripsAllMethods) {
  for (auto m :
       {HttpMethod::Get, HttpMethod::Head, HttpMethod::Post, HttpMethod::Put, HttpMethod::Delete,
        HttpMethod::Options, HttpMethod::Patch, HttpMethod::Connect, HttpMethod::Trace}) {
    auto parsed = method_from_string(to_string(m));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, m);
  }
  EXPECT_FALSE(method_from_string("get").has_value()) << "methods are case-sensitive";
  EXPECT_FALSE(method_from_string("BREW").has_value());
  EXPECT_EQ(version_from_string("HTTP/1.1"), HttpVersion::Http11);
  EXPECT_EQ(version_from_string("HTTP/1.0"), HttpVersion::Http10);
  EXPECT_FALSE(version_from_string("HTTP/2").has_value());
}

TEST(HttpRequest, HeaderLookupIsCaseInsensitive) {
  HttpRequest req;
  req.headers = {{"Host", "example.com"}, {"content-length", "12"}};
  EXPECT_EQ(req.header("HOST"), "example.com");
  EXPECT_EQ(req.header("Content-Length"), "12");
  EXPECT_FALSE(req.header("X-Missing").has_value());
  EXPECT_EQ(req.content_length(), 12U);
  req.headers[1].value = "12abc";
  EXPECT_FALSE(req.content_length().has_value());
}

TEST(HttpRequest, KeepAliveDefaults) {
  HttpRequest req;
  req.version = HttpVersion::Http11;
  EXPECT_TRUE(req.keep_alive());
  req.headers = {{"Connection", "Close"}};
  EXPECT_FALSE(req.keep_alive());
  req.version = HttpVersion::Http10;
  req.headers.clear();
  EXPECT_FALSE(req.keep_alive());
  req.headers = {{"Connection", "keep-alive"}};
  EXPECT_TRUE(req.keep_alive());
}

TEST(HttpResponse, SerializesWithContentLength) {
  HttpResponse resp;
  resp.status_code = 200;
  resp.headers = {{"Content-Type", "text/plain"}};
  resp.body = "hi";
  EXPECT_EQ(resp.serialize(),
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nhi");
}

TEST(HttpResponse, ErrorResponseIsWellFormed) {
  const HttpResponse resp = make_error_response(502, "upstream down");
  EXPECT_EQ(resp.status_code, 502);
  EXPECT_EQ(resp.reason, "Bad Gateway");
  EXPECT_EQ(resp.header("Connection"), "close");
  const std::string wire = resp.serialize();
  EXPECT_EQ(wire.rfind("HTTP/1.1 502 Bad Gateway\r\n", 0), 0U);
  EXPECT_NE(wire.find("\r\n\r\n502 Bad Gateway\nupstream down\n"), std::string::npos);
}

// ---- Implemented now: request line + headers (Member 2, Week 1) ----

TEST(HttpParser, TakeRequestFailsBeforeComplete) {
  HttpParser p;
  EXPECT_EQ(p.state(), HttpParser::State::RequestLine);
  EXPECT_EQ(p.take_request().error().code, ErrorCode::InvalidState);
}

TEST(HttpParser, ParsesSimpleGet) {
  HttpParser p;
  constexpr std::string_view kRaw = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
  auto r = p.feed(kRaw);
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->complete);
  EXPECT_EQ(r->bytes_consumed, kRaw.size());
  auto req = p.take_request();
  ASSERT_TRUE(req);
  EXPECT_EQ(req->method, HttpMethod::Get);
  EXPECT_EQ(req->target, "/index.html");
  EXPECT_EQ(req->version, HttpVersion::Http11);
  EXPECT_EQ(req->header("host"), "example.com");
  EXPECT_EQ(p.state(), HttpParser::State::RequestLine) << "reset for keep-alive";
}

TEST(HttpParser, ParsesHttp10AndMultipleHeadersWithTrimmedOws) {
  HttpParser p;
  constexpr std::string_view kRaw =
      "HEAD /status?ok=1 HTTP/1.0\r\n"
      "Host:   example.com \t\r\n"
      "X-Trace-Id: abc-123\r\n"
      "X-Empty:\r\n"
      "\r\n";
  auto r = p.feed(kRaw);
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->complete);
  EXPECT_EQ(r->bytes_consumed, kRaw.size());
  auto req = p.take_request();
  ASSERT_TRUE(req);
  EXPECT_EQ(req->method, HttpMethod::Head);
  EXPECT_EQ(req->target, "/status?ok=1");
  EXPECT_EQ(req->version, HttpVersion::Http10);
  EXPECT_EQ(req->header("Host"), "example.com");
  EXPECT_EQ(req->header("x-trace-id"), "abc-123");
  EXPECT_EQ(req->header("X-Empty"), "");
  EXPECT_FALSE(req->keep_alive());
}

TEST(HttpParser, HandlesPartialRequestLineAndHeaders) {
  HttpParser p;
  std::string buffer;

  const auto feed_chunk = [&](std::string_view chunk) {
    buffer.append(chunk);
    auto r = p.feed(buffer);
    if (r) {
      buffer.erase(0, r->bytes_consumed);
    }
    return r;
  };

  auto r1 = feed_chunk("GET /hel");
  ASSERT_TRUE(r1);
  EXPECT_FALSE(r1->complete);
  EXPECT_EQ(r1->bytes_consumed, 0U);

  auto r2 = feed_chunk("lo HTTP/1.1\r\nHo");
  ASSERT_TRUE(r2);
  EXPECT_FALSE(r2->complete);
  EXPECT_EQ(p.state(), HttpParser::State::Headers);

  auto r3 = feed_chunk("st: example.com\r\nX-A: 1\r");
  ASSERT_TRUE(r3);
  EXPECT_FALSE(r3->complete);

  auto r4 = feed_chunk("\n\r\nGET /second HTTP/1.1\r\n\r\n");
  ASSERT_TRUE(r4);
  EXPECT_TRUE(r4->complete);
  EXPECT_EQ(buffer, "GET /second HTTP/1.1\r\n\r\n") << "must not consume next pipelined request";

  auto req1 = p.take_request();
  ASSERT_TRUE(req1);
  EXPECT_EQ(req1->target, "/hello");
  EXPECT_EQ(req1->header("host"), "example.com");
  EXPECT_EQ(req1->header("x-a"), "1");

  auto r5 = p.feed(buffer);
  ASSERT_TRUE(r5);
  EXPECT_TRUE(r5->complete);
  EXPECT_EQ(r5->bytes_consumed, buffer.size());
  auto req2 = p.take_request();
  ASSERT_TRUE(req2);
  EXPECT_EQ(req2->target, "/second");
}

TEST(HttpParser, AcceptsZeroContentLengthImmediately) {
  HttpParser p;
  constexpr std::string_view kRaw = "POST /empty HTTP/1.1\r\nContent-Length: 0\r\n\r\n";
  auto r = p.feed(kRaw);
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->complete);
  EXPECT_EQ(r->bytes_consumed, kRaw.size());
  auto req = p.take_request();
  ASSERT_TRUE(req);
  EXPECT_EQ(req->method, HttpMethod::Post);
  EXPECT_TRUE(req->body.empty());
}

TEST(HttpParser, RejectsMalformedRequestLine) {
  HttpParser p;
  auto r = p.feed("GET\r\n\r\n");
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::ParseError);
  EXPECT_EQ(p.state(), HttpParser::State::Failed);
  EXPECT_EQ(p.feed("GET / HTTP/1.1\r\n\r\n").error().code, ErrorCode::InvalidState);
  p.reset();
  EXPECT_EQ(p.state(), HttpParser::State::RequestLine);
}

TEST(HttpParser, RejectsBadVersionMethodTargetAndBareLf) {
  for (std::string_view bad : {
           std::string_view{"get / HTTP/1.1\r\n\r\n"},
           std::string_view{"GET / HTTP/2\r\n\r\n"},
           std::string_view{"GET  / HTTP/1.1\r\n\r\n"},
           std::string_view{"GET / a HTTP/1.1\r\n\r\n"},
           std::string_view{"GET / HTTP/1.1\n\n"},
           std::string_view{"GET / HTTP/1.1\r\nBadHeaderNoColon\r\n\r\n"},
           std::string_view{"GET / HTTP/1.1\r\nBad Name: v\r\n\r\n"},
           std::string_view{"GET / HTTP/1.1\r\n: no-name\r\n\r\n"},
           std::string_view{"GET / HTTP/1.1\r\nContent-Length: -1\r\n\r\n"},
           std::string_view{"GET / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n"},
           std::string_view{
               "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n"},
       }) {
    HttpParser p;
    auto r = p.feed(bad);
    ASSERT_FALSE(r) << "expected ParseError for: " << bad;
    EXPECT_EQ(r.error().code, ErrorCode::ParseError) << bad;
    EXPECT_EQ(p.state(), HttpParser::State::Failed) << bad;
  }
}

TEST(HttpParser, RejectsOversizedRequestLine) {
  HttpParser p;
  std::string huge = "GET /" + std::string(HttpParser::kMaxRequestLineBytes, 'a');
  auto r = p.feed(huge);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::ParseError);
  EXPECT_EQ(p.state(), HttpParser::State::Failed);
}

TEST(HttpParser, RejectsOversizedHeaders) {
  HttpParser p;
  std::string huge = "GET / HTTP/1.1\r\n";
  huge += "X-Pad: " + std::string(HttpParser::kMaxHeaderBytes, 'a') + "\r\n\r\n";
  auto r = p.feed(huge);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::ParseError);
}

TEST(HttpParser, RejectsTooManyHeaders) {
  HttpParser p;
  std::string req = "GET / HTTP/1.1\r\n";
  for (std::size_t i = 0; i <= HttpParser::kMaxHeaderCount; ++i) {
    req += "X-H" + std::to_string(i) + ": 1\r\n";
  }
  req += "\r\n";
  auto r = p.feed(req);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::ParseError);
  EXPECT_EQ(p.state(), HttpParser::State::Failed);
}

// ---- Implemented now: Content-Length body + partial input (Member 2, Week 2) ----

TEST(HttpParser, HandlesPartialInputAcrossFeeds) {
  HttpParser p;
  auto r1 = p.feed("POST /a HTTP/1.1\r\nContent-Len");
  ASSERT_TRUE(r1);
  EXPECT_FALSE(r1->complete);
  EXPECT_EQ(r1->bytes_consumed, 18U);
  auto r2 = p.feed("Content-Length: 5\r\n\r\nhel");
  ASSERT_TRUE(r2);
  EXPECT_FALSE(r2->complete);
  EXPECT_EQ(r2->bytes_consumed, 24U);
  auto r3 = p.feed("lo");
  ASSERT_TRUE(r3);
  EXPECT_TRUE(r3->complete);
  EXPECT_EQ(r3->bytes_consumed, 2U);
  auto req = p.take_request();
  ASSERT_TRUE(req);
  EXPECT_EQ(req->method, HttpMethod::Post);
  EXPECT_EQ(req->target, "/a");
  EXPECT_EQ(req->body, "hello");
}

TEST(HttpParser, ParsesFixedBodyByteByByteAndPreservesNextRequest) {
  HttpParser p;
  const std::string first =
      "POST /items HTTP/1.1\r\nHost: localhost\r\nContent-Length: 11\r\n\r\n"
      "hello world";
  const std::string second = "GET /next HTTP/1.1\r\nHost: localhost\r\n\r\n";
  const std::string stream = first + second;

  std::string buffer;
  bool done = false;
  std::size_t stream_pos = 0;
  for (; stream_pos < stream.size() && !done; ++stream_pos) {
    buffer.push_back(stream[stream_pos]);
    auto r = p.feed(buffer);
    ASSERT_TRUE(r);
    buffer.erase(0, r->bytes_consumed);
    done = r->complete;
  }
  ASSERT_TRUE(done);
  EXPECT_EQ(stream_pos, first.size());
  EXPECT_TRUE(buffer.empty());

  auto req1 = p.take_request();
  ASSERT_TRUE(req1);
  EXPECT_EQ(req1->method, HttpMethod::Post);
  EXPECT_EQ(req1->target, "/items");
  EXPECT_EQ(req1->body, "hello world");

  // Also test when both requests arrive in a single buffer.
  auto combined = p.feed(stream);
  ASSERT_TRUE(combined);
  EXPECT_TRUE(combined->complete);
  EXPECT_EQ(combined->bytes_consumed, first.size());
  EXPECT_EQ(p.take_request()->body, "hello world");

  auto next = p.feed(std::string_view{stream}.substr(combined->bytes_consumed));
  ASSERT_TRUE(next);
  EXPECT_TRUE(next->complete);
  EXPECT_EQ(next->bytes_consumed, second.size());
  EXPECT_EQ(p.take_request()->target, "/next");
}

TEST(HttpParser, RejectsOversizedContentLengthBody) {
  HttpParser p;
  const std::string req =
      "POST /huge HTTP/1.1\r\nContent-Length: " + std::to_string(HttpParser::kMaxBodyBytes + 1) +
      "\r\n\r\n";
  auto r = p.feed(req);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::ParseError);
  EXPECT_EQ(p.state(), HttpParser::State::Failed);
}

TEST(HttpParser, ChunkedBodyParsingIsNotImplementedYet) {
  HttpParser chunked;
  auto r = chunked.feed("POST /c HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::NotImplemented);
}

// ---- Pending (Member 2, Week 4): enable as chunked body parsing lands ----

TEST(HttpParser, DISABLED_ParsesChunkedBody) {
  HttpParser p;
  auto r = p.feed("POST /c HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->complete);
  EXPECT_EQ(p.take_request()->body, "hello");
}

}  // namespace aegis::http
