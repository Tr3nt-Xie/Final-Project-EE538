#pragma once

#include "aegisproxy/balancer/backend_registry.hpp"
#include "aegisproxy/common/result.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace aegis::health {

struct HealthConfig {
  std::chrono::milliseconds interval{2000};
  std::chrono::milliseconds timeout{500};
  unsigned healthy_threshold{2};    ///< consecutive successes to become Healthy
  unsigned unhealthy_threshold{3};  ///< consecutive failures to become Unhealthy
  std::string probe_path{"/health"};
};

enum class ProbeOutcome : std::uint8_t { Success, Failure, Timeout };

/// Active health checking.
///
/// Design: the monitor is a pure state machine over probe outcomes so that it can be
/// unit-tested without sockets. The I/O that produces outcomes (non-blocking connect
/// + "GET /health" with a timeout, scheduled every `interval`) is a separate concern:
///
///   * tick(now) decides which backends are due for a probe and returns their ids;
///   * the caller (main / a HealthProber helper, TODO) performs the probe through the
///     EventLoop and reports record_probe_result(id, outcome);
///   * record_probe_result applies the consecutive-count thresholds and calls
///     BackendRegistry::set_health, which fans out to the strategies.
///
/// Automatic failover = strategies reacting to on_health_changed (they drop Unhealthy
/// backends from their candidate sets) + RequestForwarder retrying on connect failure.
///
/// OWNER: Member 3. tick() and record_probe_result() return NotImplemented until
/// written; a timer facility on EventLoop (Member 1) is required for scheduling.
///
/// Thread-safety: none.
class HealthMonitor {
 public:
  HealthMonitor(lb::BackendRegistry& registry, HealthConfig config);

  /// Marks every backend as due immediately. Does not perform any I/O.
  Status start();
  Status stop();
  [[nodiscard]] bool is_running() const noexcept { return running_; }

  /// Returns the backends whose next probe time has passed.
  /// TODO(Member 3): compare per-backend next_probe_at with `now`.
  Result<std::vector<BackendId>> tick(std::chrono::steady_clock::time_point now);

  /// Applies one probe outcome and updates registry health per the thresholds.
  /// TODO(Member 3): consecutive counters; Unknown→Healthy after healthy_threshold
  /// successes; Healthy→Unhealthy after unhealthy_threshold failures; Unhealthy→Healthy
  /// after healthy_threshold successes; reset the opposite counter on each outcome.
  Status record_probe_result(BackendId id, ProbeOutcome outcome);

  [[nodiscard]] const HealthConfig& config() const noexcept { return config_; }

 private:
  struct Track {
    unsigned consecutive_successes{0};
    unsigned consecutive_failures{0};
    std::chrono::steady_clock::time_point next_probe_at{};
  };

  lb::BackendRegistry& registry_;
  HealthConfig config_;
  std::vector<Track> tracks_;  ///< indexed by BackendId
  bool running_{false};
};

}  // namespace aegis::health
