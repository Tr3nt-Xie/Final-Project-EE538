#pragma once

#include "aegisproxy/common/types.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace aegis::lb {

/// Static description of an upstream server (from config).
struct BackendEndpoint {
  std::string name;
  std::string host;  ///< IPv4 literal for now (see socket_ops::make_ipv4 TODO)
  std::uint16_t port{0};
  std::uint32_t weight{1};
};

/// Health as determined by HealthMonitor. Only Healthy backends are eligible for
/// selection; Draining ones finish in-flight requests but receive no new ones.
enum class HealthState : std::uint8_t { Unknown, Healthy, Unhealthy, Draining };

[[nodiscard]] std::string_view to_string(HealthState state) noexcept;

/// Runtime counters, updated by BackendRegistry::on_connection_started/finished.
struct BackendStats {
  std::uint32_t active_connections{0};
  std::uint64_t total_requests{0};
  std::uint64_t failed_requests{0};
  double ewma_latency_ms{0.0};  ///< TODO(Member 3): maintained by AdaptiveBalancer
};

/// Outcome of one proxied request, reported when the backend side completes.
struct CompletionInfo {
  std::chrono::microseconds latency{0};
  bool success{true};
};

struct BackendEntry {
  BackendId id{kInvalidBackendId};
  BackendEndpoint endpoint;
  HealthState health{HealthState::Unknown};
  BackendStats stats;
};

}  // namespace aegis::lb
