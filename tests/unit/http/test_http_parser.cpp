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

// ---- Implemented now: parser scaffold behaviour ----

TEST(HttpParser, FeedIsExplicitlyUnimplemented) {
  HttpParser p;
  EXPECT_EQ(p.state(), HttpParser::State::RequestLine);
  auto r = p.feed("GET / HTTP/1.1\r\n\r\n");
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(p.take_request().error().code, ErrorCode::InvalidState);
}

// ---- Pending (Member 2): enable as the parser state machine lands ----

TEST(HttpParser, DISABLED_ParsesSimpleGet) {
  HttpParser p;
  auto r = p.feed("GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n");
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->complete);
  EXPECT_EQ(r->bytes_consumed, 45U);
  auto req = p.take_request();
  ASSERT_TRUE(req);
  EXPECT_EQ(req->method, HttpMethod::Get);
  EXPECT_EQ(req->target, "/index.html");
  EXPECT_EQ(req->version, HttpVersion::Http11);
  EXPECT_EQ(req->header("host"), "example.com");
  EXPECT_EQ(p.state(), HttpParser::State::RequestLine) << "reset for keep-alive";
}

TEST(HttpParser, DISABLED_HandlesPartialInputAcrossFeeds) {
  HttpParser p;
  auto r1 = p.feed("POST /a HTTP/1.1\r\nContent-Len");
  ASSERT_TRUE(r1);
  EXPECT_FALSE(r1->complete);
  auto r2 = p.feed("Content-Length: 5\r\n\r\nhel");
  ASSERT_TRUE(r2);
  EXPECT_FALSE(r2->complete);
  auto r3 = p.feed("lo");
  ASSERT_TRUE(r3);
  EXPECT_TRUE(r3->complete);
  EXPECT_EQ(p.take_request()->body, "hello");
}

TEST(HttpParser, DISABLED_ParsesChunkedBody) {
  HttpParser p;
  auto r = p.feed("POST /c HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->complete);
  EXPECT_EQ(p.take_request()->body, "hello");
}

TEST(HttpParser, DISABLED_RejectsMalformedRequestLine) {
  HttpParser p;
  auto r = p.feed("GET\r\n\r\n");
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::ParseError);
  EXPECT_EQ(p.state(), HttpParser::State::Failed);
  p.reset();
  EXPECT_EQ(p.state(), HttpParser::State::RequestLine);
}

TEST(HttpParser, DISABLED_RejectsOversizedHeaders) {
  HttpParser p;
  std::string huge = "GET / HTTP/1.1\r\n";
  huge += "X-Pad: " + std::string(HttpParser::kMaxHeaderBytes, 'a') + "\r\n\r\n";
  auto r = p.feed(huge);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, ErrorCode::ParseError);
}

}  // namespace aegis::http
