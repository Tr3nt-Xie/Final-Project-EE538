#pragma once

#include "aegisproxy/balancer/load_balancing_strategy.hpp"
#include "aegisproxy/datastructures/indexed_min_heap.hpp"

namespace aegis::lb {

/// Picks the Healthy backend with the fewest active connections, in O(log N) using
/// IndexedMinHeap<BackendId, active_connections>.
///
/// TODO(Member 3):
///   on_backend_added / health→Healthy: heap_.insert(id, active)
///   health→Unhealthy/Draining:         heap_.remove(id)
///   on_connection_started/finished:    heap_.update_key(id, new active count)
///   select():                          heap_.peek_min()
/// Ties: break by weight or registration order — document the choice.
class LeastConnectionsBalancer final : public LoadBalancingStrategy {
 public:
  explicit LeastConnectionsBalancer(BackendRegistry& registry);

  [[nodiscard]] std::string_view name() const noexcept override { return "least_connections"; }
  [[nodiscard]] Result<BackendId> select(const SelectionContext& context) override;

  void on_backend_added(BackendId id) override;
  void on_health_changed(BackendId id, HealthState previous, HealthState current) override;
  void on_connection_started(BackendId id) override;
  void on_connection_finished(BackendId id, const CompletionInfo& info) override;

 private:
  [[maybe_unused]] ds::IndexedMinHeap<BackendId, std::uint32_t> heap_;  // TODO(Member 3)
};

}  // namespace aegis::lb
