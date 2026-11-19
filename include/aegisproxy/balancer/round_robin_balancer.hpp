#pragma once

#include "aegisproxy/balancer/load_balancing_strategy.hpp"

namespace aegis::lb {

/// Cycles through Healthy backends in registration order, honouring weight.
///
/// TODO(Member 3): keep a cursor into registry().entries(); on select(), advance until
/// a Healthy entry is found (at most N steps); return NoHealthyBackend if none.
/// Weighted variant: smooth weighted round-robin (as in nginx) is the reference.
class RoundRobinBalancer final : public LoadBalancingStrategy {
 public:
  explicit RoundRobinBalancer(BackendRegistry& registry);

  [[nodiscard]] std::string_view name() const noexcept override { return "round_robin"; }
  [[nodiscard]] Result<BackendId> select(const SelectionContext& context) override;

  void on_backend_added(BackendId id) override;
  void on_health_changed(BackendId id, HealthState previous, HealthState current) override;

 private:
  [[maybe_unused]] std::size_t cursor_{0};  // TODO(Member 3): drop the attribute once used
};

}  // namespace aegis::lb
