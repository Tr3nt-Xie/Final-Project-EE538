// Integration tests (Member 2): HTTP requests through the real proxy stack.
#include "aegisproxy/balancer/backend_registry.hpp"
#include "aegisproxy/balancer/round_robin_balancer.hpp"
#include "aegisproxy/proxy/backend_connector.hpp"
#include "aegisproxy/proxy/http_proxy.hpp"

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

}  // namespace

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
