#include "aegisproxy/balancer/strategy_factory.hpp"

#include "aegisproxy/balancer/adaptive_balancer.hpp"
#include "aegisproxy/balancer/least_connections_balancer.hpp"
#include "aegisproxy/balancer/round_robin_balancer.hpp"

#include <string>

namespace aegis::lb {

Result<std::unique_ptr<LoadBalancingStrategy>> make_strategy(std::string_view name,
                                                             BackendRegistry& registry) {
  if (name == "round_robin")
    return std::unique_ptr<LoadBalancingStrategy>(new RoundRobinBalancer(registry));
  if (name == "least_connections") {
    return std::unique_ptr<LoadBalancingStrategy>(new LeastConnectionsBalancer(registry));
  }
  if (name == "adaptive") {
    return std::unique_ptr<LoadBalancingStrategy>(
        new AdaptiveBalancer(registry, AdaptiveBalancer::Params{}));
  }
  return Error{ErrorCode::InvalidArgument, "unknown strategy '" + std::string(name) + "'"};
}

}  // namespace aegis::lb
