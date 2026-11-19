#include "aegisproxy/balancer/backend_registry.hpp"

#include <vector>

#include <gtest/gtest.h>

namespace aegis::lb {

namespace {
struct RecordingObserver final : BackendObserver {
  struct HealthEvent {
    BackendId id;
    HealthState from;
    HealthState to;
  };
  std::vector<BackendId> added;
  std::vector<HealthEvent> health;
  std::vector<BackendId> started;
  std::vector<BackendId> finished;

  void on_backend_added(BackendId id) override { added.push_back(id); }
  void on_health_changed(BackendId id, HealthState from, HealthState to) override {
    health.push_back({id, from, to});
  }
  void on_connection_started(BackendId id) override { started.push_back(id); }
  void on_connection_finished(BackendId id, const CompletionInfo&) override {
    finished.push_back(id);
  }
};

BackendEndpoint ep(const char* name, std::uint16_t port) {
  return BackendEndpoint{name, "127.0.0.1", port, 1};
}
}  // namespace

TEST(BackendRegistry, AddAssignsDenseIds) {
  BackendRegistry r;
  EXPECT_EQ(r.add(ep("a", 1)), 0U);
  EXPECT_EQ(r.add(ep("b", 2)), 1U);
  EXPECT_EQ(r.size(), 2U);
  ASSERT_NE(r.find(1), nullptr);
  EXPECT_EQ(r.find(1)->endpoint.name, "b");
  EXPECT_EQ(r.find(1)->health, HealthState::Unknown);
  EXPECT_EQ(r.find(7), nullptr);
  EXPECT_EQ(r.find(kInvalidBackendId), nullptr);
}

TEST(BackendRegistry, HealthChangesPropagateToObserversOnce) {
  BackendRegistry r;
  RecordingObserver obs;
  r.add_observer(obs);
  r.add_observer(obs);  // duplicate add is ignored
  EXPECT_EQ(r.observer_count(), 1U);
  const BackendId id = r.add(ep("a", 1));
  ASSERT_EQ(obs.added.size(), 1U);

  ASSERT_TRUE(r.set_health(id, HealthState::Healthy));
  ASSERT_TRUE(r.set_health(id, HealthState::Healthy));  // no-op: unchanged
  ASSERT_TRUE(r.set_health(id, HealthState::Unhealthy));
  ASSERT_EQ(obs.health.size(), 2U);
  EXPECT_EQ(obs.health[0].from, HealthState::Unknown);
  EXPECT_EQ(obs.health[0].to, HealthState::Healthy);
  EXPECT_EQ(obs.health[1].to, HealthState::Unhealthy);
  EXPECT_EQ(r.healthy_count(), 0U);
  EXPECT_EQ(r.set_health(99, HealthState::Healthy).error().code, ErrorCode::NotFound);

  r.remove_observer(obs);
  ASSERT_TRUE(r.set_health(id, HealthState::Healthy));
  EXPECT_EQ(obs.health.size(), 2U) << "removed observer must not be notified";
  EXPECT_EQ(r.healthy_count(), 1U);
}

TEST(BackendRegistry, ConnectionCountersAndObserverFanOut) {
  BackendRegistry r;
  RecordingObserver obs;
  r.add_observer(obs);
  const BackendId id = r.add(ep("a", 1));

  ASSERT_TRUE(r.on_connection_started(id));
  ASSERT_TRUE(r.on_connection_started(id));
  EXPECT_EQ(r.find(id)->stats.active_connections, 2U);
  EXPECT_EQ(r.find(id)->stats.total_requests, 2U);

  ASSERT_TRUE(r.on_connection_finished(id, CompletionInfo{std::chrono::microseconds{10}, true}));
  ASSERT_TRUE(r.on_connection_finished(id, CompletionInfo{std::chrono::microseconds{10}, false}));
  EXPECT_EQ(r.find(id)->stats.active_connections, 0U);
  EXPECT_EQ(r.find(id)->stats.failed_requests, 1U);
  EXPECT_EQ(obs.started.size(), 2U);
  EXPECT_EQ(obs.finished.size(), 2U);

  Status underflow = r.on_connection_finished(id, CompletionInfo{});
  ASSERT_FALSE(underflow);
  EXPECT_EQ(underflow.error().code, ErrorCode::InvalidState);
  EXPECT_EQ(r.on_connection_started(5).error().code, ErrorCode::NotFound);
}

}  // namespace aegis::lb
