#include "aegisproxy/health/health_monitor.hpp"

namespace aegis::health {

HealthMonitor::HealthMonitor(lb::BackendRegistry& registry, HealthConfig config)
    : registry_(registry), config_(std::move(config)) {}

Status HealthMonitor::start() {
  if (running_)
    return Error{ErrorCode::InvalidState, "HealthMonitor already running"};
  tracks_.assign(registry_.size(), Track{});
  running_ = true;
  return ok();
}

Status HealthMonitor::stop() {
  running_ = false;
  return ok();
}

Result<std::vector<BackendId>> HealthMonitor::tick(std::chrono::steady_clock::time_point now) {
  (void)now;
  if (!running_)
    return Error{ErrorCode::InvalidState, "HealthMonitor not running"};
  return Error::not_implemented("HealthMonitor::tick");
}

Status HealthMonitor::record_probe_result(BackendId id, ProbeOutcome outcome) {
  (void)outcome;
  if (!running_)
    return Error{ErrorCode::InvalidState, "HealthMonitor not running"};
  if (id >= tracks_.size())
    return Error{ErrorCode::NotFound, "unknown backend"};
  return Error::not_implemented("HealthMonitor::record_probe_result");
}

}  // namespace aegis::health
