#include "aegisproxy/balancer/backend.hpp"

namespace aegis::lb {

std::string_view to_string(HealthState state) noexcept {
  switch (state) {
    case HealthState::Unknown:
      return "Unknown";
    case HealthState::Healthy:
      return "Healthy";
    case HealthState::Unhealthy:
      return "Unhealthy";
    case HealthState::Draining:
      return "Draining";
  }
  return "?";
}

}  // namespace aegis::lb
