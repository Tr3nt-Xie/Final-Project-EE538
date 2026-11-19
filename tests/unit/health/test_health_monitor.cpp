#include "aegisproxy/health/health_monitor.hpp"

#include <gtest/gtest.h>

namespace aegis::health {

namespace {
void add_two_backends(lb::BackendRegistry& r) {
  r.add(lb::BackendEndpoint{"a", "127.0.0.1", 1, 1});
  r.add(lb::BackendEndpoint{"b", "127.0.0.1", 2, 1});
}
HealthConfig cfg() {
  return HealthConfig{std::chrono::milliseconds{100}, std::chrono::milliseconds{50}, 2, 3,
                      "/health"};
}
}  // namespace

// ---- Implemented now ----

TEST(HealthMonitor, StartStopLifecycle) {
  lb::BackendRegistry r;
  add_two_backends(r);
  HealthMonitor m(r, cfg());
  EXPECT_FALSE(m.is_running());
  ASSERT_TRUE(m.start());
  EXPECT_TRUE(m.is_running());
  EXPECT_EQ(m.start().error().code, ErrorCode::InvalidState);
  ASSERT_TRUE(m.stop());
  EXPECT_FALSE(m.is_running());
}

TEST(HealthMonitor, OperationsRequireRunning) {
  lb::BackendRegistry r;
  add_two_backends(r);
  HealthMonitor m(r, cfg());
  EXPECT_EQ(m.tick(std::chrono::steady_clock::now()).error().code, ErrorCode::InvalidState);
  EXPECT_EQ(m.record_probe_result(0, ProbeOutcome::Success).error().code, ErrorCode::InvalidState);
}

TEST(HealthMonitor, LogicIsExplicitlyUnimplemented) {
  lb::BackendRegistry r;
  add_two_backends(r);
  HealthMonitor m(r, cfg());
  ASSERT_TRUE(m.start());
  EXPECT_EQ(m.tick(std::chrono::steady_clock::now()).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(m.record_probe_result(0, ProbeOutcome::Success).error().code,
            ErrorCode::NotImplemented);
  EXPECT_EQ(m.record_probe_result(9, ProbeOutcome::Success).error().code, ErrorCode::NotFound);
  EXPECT_EQ(r.find(0)->health, lb::HealthState::Unknown) << "no silent health change";
}

// ---- Pending (Member 3) ----

TEST(HealthMonitor, DISABLED_BecomesHealthyAfterThresholdSuccesses) {
  lb::BackendRegistry r;
  add_two_backends(r);
  HealthMonitor m(r, cfg());
  ASSERT_TRUE(m.start());
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Success));
  EXPECT_EQ(r.find(0)->health, lb::HealthState::Unknown) << "threshold is 2";
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Success));
  EXPECT_EQ(r.find(0)->health, lb::HealthState::Healthy);
}

TEST(HealthMonitor, DISABLED_BecomesUnhealthyAfterThresholdFailures) {
  lb::BackendRegistry r;
  add_two_backends(r);
  HealthMonitor m(r, cfg());
  ASSERT_TRUE(m.start());
  ASSERT_TRUE(r.set_health(0, lb::HealthState::Healthy));
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Failure));
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Timeout));
  EXPECT_EQ(r.find(0)->health, lb::HealthState::Healthy) << "threshold is 3";
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Failure));
  EXPECT_EQ(r.find(0)->health, lb::HealthState::Unhealthy);
}

TEST(HealthMonitor, DISABLED_SuccessResetsFailureStreak) {
  lb::BackendRegistry r;
  add_two_backends(r);
  HealthMonitor m(r, cfg());
  ASSERT_TRUE(m.start());
  ASSERT_TRUE(r.set_health(0, lb::HealthState::Healthy));
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Failure));
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Failure));
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Success));
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Failure));
  ASSERT_TRUE(m.record_probe_result(0, ProbeOutcome::Failure));
  EXPECT_EQ(r.find(0)->health, lb::HealthState::Healthy);
}

TEST(HealthMonitor, DISABLED_TickReturnsDueBackends) {
  lb::BackendRegistry r;
  add_two_backends(r);
  HealthMonitor m(r, cfg());
  ASSERT_TRUE(m.start());
  const auto now = std::chrono::steady_clock::now();
  auto due = m.tick(now);
  ASSERT_TRUE(due);
  EXPECT_EQ(due->size(), 2U) << "everything is due right after start()";
  due = m.tick(now + std::chrono::milliseconds{10});
  ASSERT_TRUE(due);
  EXPECT_TRUE(due->empty()) << "not due again before the interval elapses";
}

}  // namespace aegis::health
