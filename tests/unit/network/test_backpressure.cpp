#include "aegisproxy/network/backpressure_controller.hpp"

#include <gtest/gtest.h>

namespace aegis::net {

using Decision = BackpressureController::Decision;

TEST(BackpressureController, StoresThresholds) {
  BackpressureController bp({.high_watermark_bytes = 100, .low_watermark_bytes = 20});
  EXPECT_EQ(bp.thresholds().high_watermark_bytes, 100U);
  EXPECT_EQ(bp.thresholds().low_watermark_bytes, 20U);
}

TEST(BackpressureController, EvaluateIsExplicitlyUnimplemented) {
  BackpressureController bp({});
  auto d = bp.evaluate(0, false);
  ASSERT_FALSE(d);
  EXPECT_EQ(d.error().code, ErrorCode::NotImplemented);
}

// ---- Pending (Member 1): enable once evaluate() is implemented ----

TEST(BackpressureController, DISABLED_PausesAboveHighWatermark) {
  BackpressureController bp({.high_watermark_bytes = 100, .low_watermark_bytes = 20});
  EXPECT_EQ(*bp.evaluate(100, false), Decision::PauseReads);
  EXPECT_EQ(*bp.evaluate(150, false), Decision::PauseReads);
}

TEST(BackpressureController, DISABLED_ResumesOnlyBelowLowWatermark) {
  BackpressureController bp({.high_watermark_bytes = 100, .low_watermark_bytes = 20});
  EXPECT_EQ(*bp.evaluate(50, true), Decision::Continue) << "hysteresis: still paused";
  EXPECT_EQ(*bp.evaluate(20, true), Decision::ResumeReads);
}

TEST(BackpressureController, DISABLED_ContinuesInNormalRange) {
  BackpressureController bp({.high_watermark_bytes = 100, .low_watermark_bytes = 20});
  EXPECT_EQ(*bp.evaluate(50, false), Decision::Continue);
}

}  // namespace aegis::net
