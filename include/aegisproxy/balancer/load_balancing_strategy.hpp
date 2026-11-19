#pragma once

#include "aegisproxy/balancer/backend_registry.hpp"
#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"

#include <string_view>

namespace aegis::lb {

/// Per-request hints a strategy may use (all optional; empty by default).
struct SelectionContext {
  std::string_view client_address;  ///< for future affinity / hashing
  std::string_view request_path;
};

/// Abstract backend-selection policy.
///
/// Contract:
///   * select() returns a Healthy backend id or NoHealthyBackend. It must be O(log N)
///     or better and must not allocate on the hot path once implemented.
///   * Health and load changes arrive through the BackendObserver callbacks; the
///     registry is the only input. Strategies never touch sockets or the event loop,
///     which is what lets us add or swap algorithms without changing the network core.
///   * Constructing a strategy subscribes it to the registry; destruction unsubscribes.
///
/// Thread-safety: none (event-loop thread only).
class LoadBalancingStrategy : public BackendObserver {
 public:
  explicit LoadBalancingStrategy(BackendRegistry& registry);
  ~LoadBalancingStrategy() override;

  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
  [[nodiscard]] virtual Result<BackendId> select(const SelectionContext& context) = 0;

 protected:
  [[nodiscard]] BackendRegistry& registry() noexcept { return registry_; }
  [[nodiscard]] const BackendRegistry& registry() const noexcept { return registry_; }

 private:
  BackendRegistry& registry_;
};

}  // namespace aegis::lb
