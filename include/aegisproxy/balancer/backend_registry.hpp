#pragma once

#include "aegisproxy/balancer/backend.hpp"
#include "aegisproxy/common/result.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace aegis::lb {

/// Receives registry change notifications. LoadBalancingStrategy and HealthMonitor
/// both implement this; the registry is the single hub through which health and
/// load information flows, so strategies never talk to the network core.
class BackendObserver {
 public:
  virtual ~BackendObserver() = default;
  BackendObserver(const BackendObserver&) = delete;
  BackendObserver& operator=(const BackendObserver&) = delete;

  virtual void on_backend_added(BackendId id) { (void)id; }
  virtual void on_health_changed(BackendId id, HealthState previous, HealthState current) = 0;
  virtual void on_connection_started(BackendId id) { (void)id; }
  virtual void on_connection_finished(BackendId id, const CompletionInfo& info) {
    (void)id;
    (void)info;
  }

 protected:
  BackendObserver() = default;
};

/// Source of truth for backends: endpoints, health, and load counters.
///
/// Ownership: created by main(); outlives strategies, HealthMonitor and HttpProxy.
/// Observers register raw pointers and MUST remove themselves before destruction
/// (LoadBalancingStrategy's base class does this automatically).
///
/// Backend ids are dense indices into entries_, stable for the registry's lifetime;
/// removal is intentionally unsupported (use HealthState::Draining instead).
///
/// Thread-safety: none (event-loop thread only).
class BackendRegistry {
 public:
  BackendRegistry() = default;
  BackendRegistry(const BackendRegistry&) = delete;
  BackendRegistry& operator=(const BackendRegistry&) = delete;

  BackendId add(BackendEndpoint endpoint);

  [[nodiscard]] const BackendEntry* find(BackendId id) const noexcept;
  [[nodiscard]] std::span<const BackendEntry> entries() const noexcept { return entries_; }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] std::size_t healthy_count() const noexcept;

  /// Called by HealthMonitor. Notifies observers only if the state actually changed.
  Status set_health(BackendId id, HealthState state);

  /// Called by HttpProxy/RequestForwarder when a backend connection is opened/closed.
  /// This is how connection completion reaches the load statistics (and, through the
  /// observer, LeastConnectionsBalancer / AdaptiveBalancer).
  Status on_connection_started(BackendId id);
  Status on_connection_finished(BackendId id, const CompletionInfo& info);

  void add_observer(BackendObserver& observer);
  void remove_observer(BackendObserver& observer) noexcept;
  [[nodiscard]] std::size_t observer_count() const noexcept { return observers_.size(); }

 private:
  [[nodiscard]] BackendEntry* find_mutable(BackendId id) noexcept;

  std::vector<BackendEntry> entries_;
  std::vector<BackendObserver*> observers_;
};

}  // namespace aegis::lb
