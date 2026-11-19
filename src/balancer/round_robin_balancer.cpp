#include "aegisproxy/balancer/round_robin_balancer.hpp"

namespace aegis::lb {

RoundRobinBalancer::RoundRobinBalancer(BackendRegistry& registry)
    : LoadBalancingStrategy(registry) {}

Result<BackendId> RoundRobinBalancer::select(const SelectionContext& context) {
  (void)context;
  // TODO(Member 3): see header.
  return Error::not_implemented("RoundRobinBalancer::select");
}

void RoundRobinBalancer::on_backend_added(BackendId id) {
  (void)id;  // TODO(Member 3): nothing needed for plain RR; weighted RR may precompute.
}

void RoundRobinBalancer::on_health_changed(BackendId id, HealthState previous,
                                           HealthState current) {
  (void)id;
  (void)previous;
  (void)current;  // TODO(Member 3): plain RR reads health from the registry at select time.
}

}  // namespace aegis::lb
