// Integration tests (Member 3): health checks and automatic failover through the proxy.
//
// Every test here is DISABLED_ until (a) Member 2's forwarding and (b) Member 3's
// HealthMonitor transitions + strategy selection exist. They are written against the
// public interfaces only, so they can be enabled without modification. Until then the
// failure-injection scenarios can be exercised at unit level by driving
// BackendRegistry::set_health and HealthMonitor::record_probe_result directly
// (see tests/unit/health and tests/unit/balancer).
#include "aegisproxy/balancer/backend_registry.hpp"
#include "aegisproxy/balancer/strategy_factory.hpp"
#include "aegisproxy/health/health_monitor.hpp"
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

std::string ok_response(std::string_view tag) {
  return "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nX-Backend: " + std::string(tag) + "\r\n\r\n";
}

struct FailoverStack {
  ProxyFixture f;
  lb::BackendRegistry registry;
  std::unique_ptr<lb::LoadBalancingStrategy> strategy;
  health::HealthMonitor monitor;
  proxy::BackendConnector connector;
  std::unique_ptr<proxy::HttpProxy> http_proxy;

  FailoverStack(std::string_view strategy_name, std::initializer_list<std::uint16_t> ports)
      : monitor(registry, health::HealthConfig{std::chrono::milliseconds{50},
                                               std::chrono::milliseconds{50}, 1, 1, "/health"}) {
    for (std::uint16_t p : ports) {
      registry.add(lb::BackendEndpoint{"b" + std::to_string(p), "127.0.0.1", p, 1});
    }
    strategy = std::move(lb::make_strategy(strategy_name, registry)).value();
    http_proxy = std::make_unique<proxy::HttpProxy>(*f.mgr, registry, *strategy, connector);
    http_proxy->attach();
  }

  std::string roundtrip(std::string_view request) {
    net::SocketHandle client = f.connect_client();
    (void)pump_until(*f.loop, [&] { return f.mgr->size() >= 1; });
    (void)::send(client.get(), request.data(), request.size(), 0);
    (void)pump_until(*f.loop, [&] { return f.mgr->size() == 0; }, 100);
    return read_until_close(client.get());
  }
};

constexpr const char* kGet = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

}  // namespace

TEST(Failover, DISABLED_TrafficMovesAwayFromAKilledBackend) {
  StubBackend a(ok_response("a"));
  StubBackend b(ok_response("b"));
  FailoverStack s("round_robin", {a.port(), b.port()});
  ASSERT_TRUE(s.f.ok());
  ASSERT_TRUE(s.registry.set_health(0, lb::HealthState::Healthy));
  ASSERT_TRUE(s.registry.set_health(1, lb::HealthState::Healthy));

  a.stop();  // "crash" backend a
  // TODO(Member 3): once probing runs on the loop, replace the manual record below by
  // pumping the loop for > unhealthy_threshold * interval and asserting the transition.
  ASSERT_TRUE(s.monitor.start());
  ASSERT_TRUE(s.monitor.record_probe_result(0, health::ProbeOutcome::Failure));
  EXPECT_EQ(s.registry.find(0)->health, lb::HealthState::Unhealthy);

  for (int i = 0; i < 4; ++i) {
    const std::string response = s.roundtrip(kGet);
    EXPECT_NE(response.find("X-Backend: b"), std::string::npos) << response;
  }
  EXPECT_EQ(b.requests_served(), 4);
}

TEST(Failover, DISABLED_ConnectFailureRetriesAnotherBackendTransparently) {
  StubBackend b(ok_response("b"));
  FailoverStack s("round_robin", {1, b.port()});  // port 1 refuses connections
  ASSERT_TRUE(s.f.ok());
  ASSERT_TRUE(s.registry.set_health(0, lb::HealthState::Healthy));  // wrongly believed healthy
  ASSERT_TRUE(s.registry.set_health(1, lb::HealthState::Healthy));
  const std::string response = s.roundtrip(kGet);
  EXPECT_EQ(response.rfind("HTTP/1.1 200 OK\r\n", 0), 0U) << "client must not see the failure";
  EXPECT_EQ(s.registry.find(0)->stats.failed_requests, 1U);
}

TEST(Failover, DISABLED_RecoveredBackendRejoinsRotation) {
  StubBackend b(ok_response("b"));
  FailoverStack s("round_robin", {1, b.port()});
  ASSERT_TRUE(s.f.ok());
  ASSERT_TRUE(s.monitor.start());
  ASSERT_TRUE(s.registry.set_health(0, lb::HealthState::Unhealthy));
  ASSERT_TRUE(s.registry.set_health(1, lb::HealthState::Healthy));
  (void)s.roundtrip(kGet);
  // Backend 0 "comes back": a success probe above healthy_threshold (1) flips it.
  StubBackend a(ok_response("a"));
  // TODO(Member 3): registry endpoints are immutable; either start `a` on a fixed port
  // chosen before the stack is built, or add a test-only endpoint override.
  ASSERT_TRUE(s.monitor.record_probe_result(0, health::ProbeOutcome::Success));
  EXPECT_EQ(s.registry.find(0)->health, lb::HealthState::Healthy);
}

TEST(Failover, DISABLED_LeastConnectionsAvoidsBusyBackend) {
  StubBackend a(ok_response("a"));
  StubBackend b(ok_response("b"));
  FailoverStack s("least_connections", {a.port(), b.port()});
  ASSERT_TRUE(s.f.ok());
  ASSERT_TRUE(s.registry.set_health(0, lb::HealthState::Healthy));
  ASSERT_TRUE(s.registry.set_health(1, lb::HealthState::Healthy));
  ASSERT_TRUE(s.registry.on_connection_started(0));  // a already has one in flight
  const std::string response = s.roundtrip(kGet);
  EXPECT_NE(response.find("X-Backend: b"), std::string::npos) << response;
}

}  // namespace aegis
