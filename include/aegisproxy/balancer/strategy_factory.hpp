#pragma once

#include "aegisproxy/balancer/load_balancing_strategy.hpp"
#include "aegisproxy/common/result.hpp"

#include <memory>
#include <string_view>

namespace aegis::lb {

/// Builds a strategy by config name: round_robin | least_connections | adaptive.
/// Error InvalidArgument for unknown names.
Result<std::unique_ptr<LoadBalancingStrategy>> make_strategy(std::string_view name,
                                                             BackendRegistry& registry);

}  // namespace aegis::lb
