#include "aegisproxy/balancer/backend_registry.hpp"

#include <algorithm>
#include <string>

namespace aegis::lb {

BackendId BackendRegistry::add(BackendEndpoint endpoint) {
  const auto id = static_cast<BackendId>(entries_.size());
  entries_.push_back(BackendEntry{id, std::move(endpoint), HealthState::Unknown, {}});
  for (BackendObserver* o : observers_)
    o->on_backend_added(id);
  return id;
}

const BackendEntry* BackendRegistry::find(BackendId id) const noexcept {
  return id < entries_.size() ? &entries_[id] : nullptr;
}

BackendEntry* BackendRegistry::find_mutable(BackendId id) noexcept {
  return id < entries_.size() ? &entries_[id] : nullptr;
}

std::size_t BackendRegistry::healthy_count() const noexcept {
  return static_cast<std::size_t>(
      std::count_if(entries_.begin(), entries_.end(),
                    [](const BackendEntry& e) { return e.health == HealthState::Healthy; }));
}

Status BackendRegistry::set_health(BackendId id, HealthState state) {
  BackendEntry* entry = find_mutable(id);
  if (entry == nullptr) {
    return Error{ErrorCode::NotFound, "set_health: unknown backend " + std::to_string(id)};
  }
  const HealthState previous = entry->health;
  if (previous == state)
    return ok();
  entry->health = state;
  for (BackendObserver* o : observers_)
    o->on_health_changed(id, previous, state);
  return ok();
}

Status BackendRegistry::on_connection_started(BackendId id) {
  BackendEntry* entry = find_mutable(id);
  if (entry == nullptr) {
    return Error{ErrorCode::NotFound, "on_connection_started: unknown backend"};
  }
  ++entry->stats.active_connections;
  ++entry->stats.total_requests;
  for (BackendObserver* o : observers_)
    o->on_connection_started(id);
  return ok();
}

Status BackendRegistry::on_connection_finished(BackendId id, const CompletionInfo& info) {
  BackendEntry* entry = find_mutable(id);
  if (entry == nullptr) {
    return Error{ErrorCode::NotFound, "on_connection_finished: unknown backend"};
  }
  if (entry->stats.active_connections == 0) {
    return Error{ErrorCode::InvalidState, "on_connection_finished without matching start"};
  }
  --entry->stats.active_connections;
  if (!info.success)
    ++entry->stats.failed_requests;
  for (BackendObserver* o : observers_)
    o->on_connection_finished(id, info);
  return ok();
}

void BackendRegistry::add_observer(BackendObserver& observer) {
  if (std::find(observers_.begin(), observers_.end(), &observer) == observers_.end()) {
    observers_.push_back(&observer);
  }
}

void BackendRegistry::remove_observer(BackendObserver& observer) noexcept {
  observers_.erase(std::remove(observers_.begin(), observers_.end(), &observer), observers_.end());
}

}  // namespace aegis::lb
