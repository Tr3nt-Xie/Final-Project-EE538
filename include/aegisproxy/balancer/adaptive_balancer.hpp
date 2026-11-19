#pragma once

#include "aegisproxy/balancer/load_balancing_strategy.hpp"
#include "aegisproxy/datastructures/indexed_min_heap.hpp"

namespace aegis::lb {

/// Scores backends by a blend of active connections and EWMA latency, so a slow but
/// lightly loaded backend is not preferred over a fast busy one.
///
/// TODO(Member 3): define score = alpha * active + (1 - alpha) * ewma_latency_ms /
/// reference_latency; update EWMA in on_connection_finished; keep backends in an
/// IndexedMinHeap<BackendId, double>; select() = peek_min(). Consider a small
/// exploration factor so a backend that was slow once can recover.
class AdaptiveBalancer final : public LoadBalancingStrategy {
 public:
  struct Params {
    double alpha{0.5};       ///< weight of connection count vs latency
    double ewma_decay{0.2};  ///< smoothing factor for latency
    double reference_latency_ms{50.0};
  };

  AdaptiveBalancer(BackendRegistry& registry, Params params);

  [[nodiscard]] std::string_view name() const noexcept override { return "adaptive"; }
  [[nodiscard]] Result<BackendId> select(const SelectionContext& context) override;

  void on_backend_added(BackendId id) override;
  void on_health_changed(BackendId id, HealthState previous, HealthState current) override;
  void on_connection_started(BackendId id) override;
  void on_connection_finished(BackendId id, const CompletionInfo& info) override;

  [[nodiscard]] const Params& params() const noexcept { return params_; }

 private:
  Params params_;
  [[maybe_unused]] ds::IndexedMinHeap<BackendId, double> heap_;  // TODO(Member 3)
};

}  // namespace aegis::lb
