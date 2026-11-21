// Integration tests (Member 2): HTTP requests through the real proxy stack.
#include "aegisproxy/balancer/backend_registry.hpp"
#include "aegisproxy/balancer/round_robin_balancer.hpp"
#include "aegisproxy/proxy/backend_connector.hpp"
#include "aegisproxy/proxy/http_proxy.hpp"
#include "aegisproxy/proxy/request_forwarder.hpp"

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <sys/socket.h>

#include "../proxy_test_support.hpp"

namespace aegis {

using testing::ProxyFixture;
using testing::pump_until;
using testing::read_until_close;
using testing::StubBackend;

namespace {

/// Full stack: fixture + registry + round-robin + proxy. `backends` are added Healthy.
struct Stack {
  ProxyFixture f;
  lb::BackendRegistry registry;
  lb::RoundRobinBalancer strategy{registry};
  proxy::BackendConnector connector;
  proxy::HttpProxy http_proxy{*f.mgr, registry, strategy, connector};

  explicit Stack(std::initializer_list<std::uint16_t> backend_ports) {
    for (std::uint16_t p : backend_ports) {
      const BackendId id =
          registry.add(lb::BackendEndpoint{"b" + std::to_string(p), "127.0.0.1", p, 1});
      (void)registry.set_health(id, lb::HealthState::Healthy);
    }
    http_proxy.attach();
  }

  std::string roundtrip(std::string_view request) {
    net::SocketHandle client = f.connect_client();
    (void)pump_until(*f.loop, [&] { return f.mgr->size() >= 1; });
    (void)::send(client.get(), request.data(), request.size(), 0);
    (void)pump_until(*f.loop, [&] { return f.mgr->size() == 0; }, 100);
    return read_until_close(client.get());
  }
};

constexpr const char* kGet = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";

/// Deterministic strategy used to test HttpProxy's Week 2 selection & response paths
/// independently of Member 3's RoundRobinBalancer implementation.
class FixedStrategy final : public lb::LoadBalancingStrategy {
 public:
  FixedStrategy(lb::BackendRegistry& reg, std::optional<BackendId> target)
      : LoadBalancingStrategy(reg), target_(target) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "fixed"; }

  [[nodiscard]] Result<BackendId> select(const lb::SelectionContext& context) override {
    (void)context;
    if (!target_) {
      return Error{ErrorCode::NoHealthyBackend, "no healthy backend"};
    }
    return *target_;
  }

  void on_health_changed(BackendId id, lb::HealthState previous, lb::HealthState current) override {
    (void)id;
    (void)previous;
    (void)current;
  }

 private:
  std::optional<BackendId> target_;
};

}  // namespace

// ---- Implemented now: Week 2 serialization and backend selection ----

TEST(RequestForwarder, SerializesRequestAndStripsHopByHopHeaders) {
  http::HttpRequest req;
  req.method = http::HttpMethod::Post;
  req.target = "/api/v1/items?k=1";
  req.version = http::HttpVersion::Http10;
  req.headers = {
      {"Host", "example.com"},
      {"Connection", "keep-alive, X-Custom-Hop"},
      {"Keep-Alive", "timeout=5"},
      {"X-Custom-Hop", "drop-me"},
      {"Transfer-Encoding", "chunked"},
      {"Proxy-Authorization", "secret"},
      {"Content-Type", "application/json"},
      {"Content-Length", "999"},
      {"X-Keep", "ok"},
  };
  req.body = R"({"a":1})";

  proxy::RequestForwarder forwarder;
  auto wire = forwarder.serialize_for_backend(req);
  ASSERT_TRUE(wire);
  EXPECT_EQ(*wire,
            "POST /api/v1/items?k=1 HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "Content-Type: application/json\r\n"
            "X-Keep: ok\r\n"
            "Connection: close\r\n"
            "Content-Length: 7\r\n"
            "\r\n"
            R"({"a":1})");
}

TEST(RequestForwarder, RejectsEmptyTargetOrInjectedCrlf) {
  proxy::RequestForwarder forwarder;

  http::HttpRequest empty_target;
  EXPECT_EQ(forwarder.serialize_for_backend(empty_target).error().code, ErrorCode::InvalidArgument);

  http::HttpRequest injected;
  injected.target = "/ok";
  injected.headers = {{"X-Bad", "a\r\nInjected: 1"}};
  EXPECT_EQ(forwarder.serialize_for_backend(injected).error().code, ErrorCode::InvalidArgument);
}

TEST(HttpProxyWeek2, SelectsBackendAndReturns502NamingIt) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
  lb::BackendRegistry registry;
  const BackendId id = registry.add(lb::BackendEndpoint{"app-1", "127.0.0.1", 9001, 1});
  (void)registry.set_health(id, lb::HealthState::Healthy);

  FixedStrategy strategy{registry, id};
  proxy::BackendConnector connector;
  proxy::HttpProxy http_proxy{*f.mgr, registry, strategy, connector};
  http_proxy.attach();

  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() >= 1; }));

  // Send a POST with Content-Length split across two writes to exercise Week 2 end to end.
  constexpr std::string_view kPart1 =
      "POST /submit HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhe";
  constexpr std::string_view kPart2 = "llo";
  (void)::send(client.get(), kPart1.data(), kPart1.size(), 0);
  (void)f.loop->run_once(std::chrono::milliseconds{20});
  EXPECT_EQ(http_proxy.requests_seen(), 0U) << "must wait until full body arrives";

  (void)::send(client.get(), kPart2.data(), kPart2.size(), 0);
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 0; }, 100));

  const std::string response = read_until_close(client.get());
  EXPECT_EQ(http_proxy.requests_seen(), 1U);
  EXPECT_EQ(response.rfind("HTTP/1.1 502 Bad Gateway\r\n", 0), 0U) << response;
  EXPECT_NE(response.find("forwarding not implemented"), std::string::npos) << response;
  EXPECT_NE(response.find("app-1"), std::string::npos) << response;
}

TEST(HttpProxyWeek2, NoHealthyBackendReturns503) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
  lb::BackendRegistry registry;
  FixedStrategy strategy{registry, std::nullopt};
  proxy::BackendConnector connector;
  proxy::HttpProxy http_proxy{*f.mgr, registry, strategy, connector};
  http_proxy.attach();

  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() >= 1; }));
  (void)::send(client.get(), kGet, std::char_traits<char>::length(kGet), 0);
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 0; }, 100));

  const std::string response = read_until_close(client.get());
  EXPECT_EQ(response.rfind("HTTP/1.1 503 Service Unavailable\r\n", 0), 0U) << response;
}

// ---- Implemented now: the scaffold fails loudly ----

TEST(HttpProxyScaffold, AnswersEveryRequestWith501AndCloses) {
  Stack s({1});
  ASSERT_TRUE(s.f.ok());
  const std::string response = s.roundtrip(kGet);
  EXPECT_EQ(s.http_proxy.requests_seen(), 1U);
  EXPECT_EQ(response.rfind("HTTP/1.1 501 Not Implemented\r\n", 0), 0U) << response;
  EXPECT_NE(response.find("Connection: close"), std::string::npos);
  EXPECT_NE(response.find("not implemented"), std::string::npos);
}

// ---- Pending (Member 2): enable as parsing + forwarding land ----

TEST(HttpProxy, DISABLED_ForwardsGetAndRelaysResponse) {
  StubBackend backend("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-Backend: stub\r\n\r\nhello");
  ASSERT_NE(backend.port(), 0);
  Stack s({backend.port()});
  ASSERT_TRUE(s.f.ok());
  const std::string response = s.roundtrip(kGet);
  EXPECT_EQ(response.rfind("HTTP/1.1 200 OK\r\n", 0), 0U) << response;
  EXPECT_NE(response.find("X-Backend: stub"), std::string::npos);
  EXPECT_NE(response.find("\r\n\r\nhello"), std::string::npos);
  EXPECT_EQ(backend.requests_served(), 1);
  EXPECT_EQ(s.registry.find(0)->stats.total_requests, 1U) << "completion reported to registry";
  EXPECT_EQ(s.registry.find(0)->stats.active_connections, 0U);
}

TEST(HttpProxy, DISABLED_MalformedRequestGets400) {
  Stack s({1});
  ASSERT_TRUE(s.f.ok());
  const std::string response = s.roundtrip("NOT HTTP\r\n\r\n");
  EXPECT_EQ(response.rfind("HTTP/1.1 400 Bad Request\r\n", 0), 0U) << response;
}

TEST(HttpProxy, DISABLED_BackendDownGets502) {
  Stack s({1});  // nothing listens on port 1
  ASSERT_TRUE(s.f.ok());
  const std::string response = s.roundtrip(kGet);
  EXPECT_EQ(response.rfind("HTTP/1.1 502 Bad Gateway\r\n", 0), 0U) << response;
  EXPECT_EQ(s.registry.find(0)->stats.failed_requests, 1U);
}

TEST(HttpProxy, DISABLED_NoHealthyBackendGets503) {
  Stack s({});
  s.registry.add(lb::BackendEndpoint{"unknown", "127.0.0.1", 1, 1});  // stays Unknown
  ASSERT_TRUE(s.f.ok());
  const std::string response = s.roundtrip(kGet);
  EXPECT_EQ(response.rfind("HTTP/1.1 503 Service Unavailable\r\n", 0), 0U) << response;
}

TEST(HttpProxy, DISABLED_PostBodyIsForwardedIntact) {
  StubBackend backend("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
  Stack s({backend.port()});
  ASSERT_TRUE(s.f.ok());
  const std::string body(3000, 'x');
  const std::string request =
      "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(body.size()) +
      "\r\n\r\n" + body;
  const std::string response = s.roundtrip(request);
  EXPECT_EQ(response.rfind("HTTP/1.1 204 No Content\r\n", 0), 0U) << response;
  // TODO(Member 2): extend StubBackend to capture the received request and assert on
  // the body length and the stripped/added headers (X-Forwarded-For).
}

}  // namespace aegis
