#include "aegisproxy/balancer/adaptive_balancer.hpp"

namespace aegis::lb {

AdaptiveBalancer::AdaptiveBalancer(BackendRegistry& registry, Params params)
    : LoadBalancingStrategy(registry), params_(params) {}

Result<BackendId> AdaptiveBalancer::select(const SelectionContext& context) {
  (void)context;
  return Error::not_implemented("AdaptiveBalancer::select");
}

void AdaptiveBalancer::on_backend_added(BackendId id) {
  (void)id;
}

void AdaptiveBalancer::on_health_changed(BackendId id, HealthState previous, HealthState current) {
  (void)id;
  (void)previous;
  (void)current;
}

void AdaptiveBalancer::on_connection_started(BackendId id) {
  (void)id;
}

void AdaptiveBalancer::on_connection_finished(BackendId id, const CompletionInfo& info) {
  (void)id;
  (void)info;  // TODO(Member 3): ewma = decay * latency + (1 - decay) * ewma; update score.
}

}  // namespace aegis::lb
