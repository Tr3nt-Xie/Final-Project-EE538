#pragma once

#include "aegisproxy/common/result.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

/// IndexedMinHeap<Key, Priority>
///
/// A binary min-heap keyed by `Priority`, where every element is also addressable by a
/// unique `Key` (for us: BackendId). The "indexed" part is a Key -> heap-position map
/// that makes remove(key) and update_key(key) O(log N) instead of O(N).
///
/// Used by LeastConnectionsBalancer (priority = active connection count) and
/// AdaptiveBalancer (priority = composite score).
///
/// Complexity targets:
///   peek_min            O(1)
///   insert / remove     O(log N)
///   update_key          O(log N)
///   contains            O(1) average
///
/// Thread-safety: none. Intended for single-threaded use from the event loop.
///
/// OWNER: Member 3. The algorithms are deliberately unimplemented in the scaffold:
/// every mutating/query operation returns ErrorCode::NotImplemented until written.
/// Do not replace this with std::priority_queue — the course requires a custom heap.
namespace aegis::ds {

template <typename Key, typename Priority, typename Compare = std::less<Priority>,
          typename KeyHash = std::hash<Key>>
class IndexedMinHeap {
 public:
  struct Entry {
    Key key;
    Priority priority;
  };

  IndexedMinHeap() = default;
  explicit IndexedMinHeap(Compare compare) : compare_(std::move(compare)) {}

  /// Inserts (key, priority). Error AlreadyExists if key is present.
  /// TODO(Member 3): append to heap_, record position in index_, sift_up(last).
  Status insert(const Key& key, const Priority& priority) {
    (void)key;
    (void)priority;
    return Error::not_implemented("IndexedMinHeap::insert");
  }

  /// Removes key. Error NotFound if absent.
  /// TODO(Member 3): swap with last, pop, erase from index_, then sift_up or sift_down
  /// the swapped-in element (it may need to move in either direction).
  Status remove(const Key& key) {
    (void)key;
    return Error::not_implemented("IndexedMinHeap::remove");
  }

  /// Changes the priority of an existing key. Error NotFound if absent.
  /// TODO(Member 3): compare old vs new priority; sift_up if it decreased, else sift_down.
  Status update_key(const Key& key, const Priority& new_priority) {
    (void)key;
    (void)new_priority;
    return Error::not_implemented("IndexedMinHeap::update_key");
  }

  /// Returns the minimum entry without removing it. Error NotFound if empty.
  /// TODO(Member 3): return heap_[0].
  Result<Entry> peek_min() const { return Error::not_implemented("IndexedMinHeap::peek_min"); }

  /// Removes and returns the minimum entry. Error NotFound if empty.
  /// TODO(Member 3): peek_min + remove(min.key), or a direct pop with sift_down(0).
  Result<Entry> extract_min() { return Error::not_implemented("IndexedMinHeap::extract_min"); }

  /// Whether key is present. O(1) average.
  /// TODO(Member 3): index_.contains(key). Until then this returns NotImplemented so a
  /// caller cannot mistake "not written yet" for "not present".
  Result<bool> contains(const Key& key) const {
    (void)key;
    return Error::not_implemented("IndexedMinHeap::contains");
  }

  [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }
  [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }
  void clear() noexcept {
    heap_.clear();
    index_.clear();
  }

  /// Debug helper for tests: true iff every parent <= its children and index_ agrees
  /// with heap_ positions. Implemented now so that tests for the TODO methods can be
  /// written against it immediately.
  [[nodiscard]] bool check_invariants() const {
    if (heap_.size() != index_.size())
      return false;
    for (std::size_t i = 0; i < heap_.size(); ++i) {
      const auto it = index_.find(heap_[i].key);
      if (it == index_.end() || it->second != i)
        return false;
      if (i > 0) {
        const std::size_t parent = (i - 1) / 2;
        // Violation: child strictly less than parent.
        if (compare_(heap_[i].priority, heap_[parent].priority))
          return false;
      }
    }
    return true;
  }

 private:
  // TODO(Member 3): implement the sift helpers.
  //   sift_up(i):   while i > 0 and heap_[i] < heap_[parent(i)]: swap_entries(i, parent)
  //   sift_down(i): while a child is smaller: swap with the smaller child
  //   swap_entries(a, b): std::swap(heap_[a], heap_[b]); update index_ for both keys
  // Optional stretch goal: replace index_ (std::unordered_map) with aegis::ds::HashTable
  // once that is implemented, so both custom structures are exercised together.

  std::vector<Entry> heap_;
  std::unordered_map<Key, std::size_t, KeyHash> index_;  ///< key -> position in heap_
  Compare compare_{};
};

}  // namespace aegis::ds
