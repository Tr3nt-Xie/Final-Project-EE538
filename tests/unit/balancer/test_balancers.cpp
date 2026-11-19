#include "aegisproxy/balancer/adaptive_balancer.hpp"
#include "aegisproxy/balancer/least_connections_balancer.hpp"
#include "aegisproxy/balancer/round_robin_balancer.hpp"
#include "aegisproxy/balancer/strategy_factory.hpp"

#include <gtest/gtest.h>

namespace aegis::lb {

namespace {
// BackendRegistry is deliberately non-movable (observers hold references to it).
void add_three_healthy(BackendRegistry& r) {
  for (std::uint16_t p = 1; p <= 3; ++p) {
    const BackendId id = r.add(BackendEndpoint{"b" + std::to_string(p), "127.0.0.1", p, 1});
    (void)r.set_health(id, HealthState::Healthy);
  }
}
}  // namespace

// ---- Implemented now ----

TEST(StrategyFactory, BuildsEachKnownStrategy) {
  BackendRegistry r;
  for (const char* name : {"round_robin", "least_connections", "adaptive"}) {
    auto s = make_strategy(name, r);
    ASSERT_TRUE(s) << name;
    EXPECT_EQ((*s)->name(), name);
    EXPECT_EQ(r.observer_count(), 1U) << "strategy subscribes on construction";
  }
  EXPECT_EQ(r.observer_count(), 0U) << "strategy unsubscribes on destruction";
}

TEST(StrategyFactory, RejectsUnknownName) {
  BackendRegistry r;
  auto s = make_strategy("random", r);
  ASSERT_FALSE(s);
  EXPECT_EQ(s.error().code, ErrorCode::InvalidArgument);
}

TEST(Balancers, SelectIsExplicitlyUnimplemented) {
  BackendRegistry r;
  add_three_healthy(r);
  RoundRobinBalancer rr(r);
  LeastConnectionsBalancer lc(r);
  AdaptiveBalancer ad(r, {});
  EXPECT_EQ(rr.select({}).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(lc.select({}).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(ad.select({}).error().code, ErrorCode::NotImplemented);
}

// ---- Pending (Member 3) ----

TEST(RoundRobinBalancer, DISABLED_CyclesThroughHealthyBackends) {
  BackendRegistry r;
  add_three_healthy(r);
  RoundRobinBalancer rr(r);
  EXPECT_EQ(*rr.select({}), 0U);
  EXPECT_EQ(*rr.select({}), 1U);
  EXPECT_EQ(*rr.select({}), 2U);
  EXPECT_EQ(*rr.select({}), 0U);
}

TEST(RoundRobinBalancer, DISABLED_SkipsUnhealthyBackends) {
  BackendRegistry r;
  add_three_healthy(r);
  RoundRobinBalancer rr(r);
  ASSERT_TRUE(r.set_health(1, HealthState::Unhealthy));
  EXPECT_EQ(*rr.select({}), 0U);
  EXPECT_EQ(*rr.select({}), 2U);
  EXPECT_EQ(*rr.select({}), 0U);
}

TEST(RoundRobinBalancer, DISABLED_NoHealthyBackendIsAnError) {
  BackendRegistry r;
  r.add(BackendEndpoint{"b", "127.0.0.1", 1, 1});  // stays Unknown
  RoundRobinBalancer rr(r);
  EXPECT_EQ(rr.select({}).error().code, ErrorCode::NoHealthyBackend);
}

TEST(LeastConnectionsBalancer, DISABLED_PicksFewestActiveConnections) {
  BackendRegistry r;
  add_three_healthy(r);
  LeastConnectionsBalancer lc(r);
  ASSERT_TRUE(r.on_connection_started(0));
  ASSERT_TRUE(r.on_connection_started(0));
  ASSERT_TRUE(r.on_connection_started(1));
  EXPECT_EQ(*lc.select({}), 2U);
  ASSERT_TRUE(r.on_connection_started(2));
  ASSERT_TRUE(r.on_connection_started(2));
  EXPECT_EQ(*lc.select({}), 1U);
  ASSERT_TRUE(r.on_connection_finished(0, CompletionInfo{}));
  ASSERT_TRUE(r.on_connection_finished(0, CompletionInfo{}));
  EXPECT_EQ(*lc.select({}), 0U);
}

TEST(LeastConnectionsBalancer, DISABLED_DropsBackendOnUnhealthy) {
  BackendRegistry r;
  add_three_healthy(r);
  LeastConnectionsBalancer lc(r);
  ASSERT_TRUE(r.on_connection_started(1));
  ASSERT_TRUE(r.on_connection_started(2));
  ASSERT_TRUE(r.set_health(0, HealthState::Unhealthy));
  const BackendId chosen = *lc.select({});
  EXPECT_NE(chosen, 0U);
}

TEST(AdaptiveBalancer, DISABLED_PrefersLowerLatencyAtEqualLoad) {
  BackendRegistry r;
  add_three_healthy(r);
  AdaptiveBalancer ad(r, {});
  for (BackendId id = 0; id < 3; ++id) {
    ASSERT_TRUE(r.on_connection_started(id));
    ASSERT_TRUE(r.on_connection_finished(
        id, CompletionInfo{std::chrono::microseconds{id == 1 ? 1000 : 50000}, true}));
  }
  EXPECT_EQ(*ad.select({}), 1U);
}

}  // namespace aegis::lb
