#include "aegisproxy/balancer/least_connections_balancer.hpp"

namespace aegis::lb {

LeastConnectionsBalancer::LeastConnectionsBalancer(BackendRegistry& registry)
    : LoadBalancingStrategy(registry) {}

Result<BackendId> LeastConnectionsBalancer::select(const SelectionContext& context) {
  (void)context;
  // TODO(Member 3): auto min = heap_.peek_min(); map NotFound → NoHealthyBackend.
  return Error::not_implemented("LeastConnectionsBalancer::select");
}

void LeastConnectionsBalancer::on_backend_added(BackendId id) {
  (void)id;  // TODO(Member 3): insert once the backend becomes Healthy.
}

void LeastConnectionsBalancer::on_health_changed(BackendId id, HealthState previous,
                                                 HealthState current) {
  (void)id;
  (void)previous;
  (void)current;  // TODO(Member 3): insert on →Healthy, remove on →Unhealthy/Draining.
}

void LeastConnectionsBalancer::on_connection_started(BackendId id) {
  (void)id;  // TODO(Member 3): heap_.update_key(id, registry().find(id)->stats.active_connections)
}

void LeastConnectionsBalancer::on_connection_finished(BackendId id, const CompletionInfo& info) {
  (void)id;
  (void)info;  // TODO(Member 3): same as above.
}

}  // namespace aegis::lb
