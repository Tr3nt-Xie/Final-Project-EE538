#pragma once

#include "aegisproxy/common/result.hpp"

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

/// HashTable<Key, Value>
///
/// A custom separate-chaining hash table with automatic resizing. Intended uses:
///   * BackendRegistry: name -> BackendId lookups
///   * ConnectionManager (later): ConnectionId -> Connection* (currently unordered_map)
///   * HttpRequest headers (later): case-insensitive name -> value
///
/// Design (to be implemented by Member 3):
///   * buckets_: std::vector of chains; each chain is a std::vector<Node> (cache-friendly
///     for the short chains we expect) — a linked list is also acceptable.
///   * load factor = size_ / buckets_.size(); when it exceeds max_load_factor_ the table
///     rehashes to roughly double the bucket count (prefer a prime or power-of-two policy
///     and document it).
///   * Iterators are invalidated by rehash; document this in the final API.
///
/// Complexity targets: insert/find/erase/contains O(1) average, O(N) worst case.
/// Thread-safety: none (single-threaded event loop).
///
/// OWNER: Member 3. All operations return ErrorCode::NotImplemented until written.
/// Do not replace this with std::unordered_map — the course requires a custom table.
namespace aegis::ds {

template <typename Key, typename Value, typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class HashTable {
 public:
  static constexpr std::size_t kDefaultBucketCount = 16;
  static constexpr float kDefaultMaxLoadFactor = 0.75F;

  HashTable() : buckets_(kDefaultBucketCount) {}
  explicit HashTable(std::size_t bucket_count)
      : buckets_(bucket_count == 0 ? kDefaultBucketCount : bucket_count) {}

  /// Inserts or overwrites. Returns true if a new key was inserted, false if updated.
  /// TODO(Member 3): hash -> bucket index; scan chain with KeyEqual; append or overwrite;
  /// ++size_; if load_factor() > max_load_factor_ then rehash(2 * bucket_count()).
  Result<bool> insert(const Key& key, Value value) {
    (void)key;
    (void)value;
    return Error::not_implemented("HashTable::insert");
  }

  /// Returns a pointer to the value, or nullptr when absent (once implemented).
  /// TODO(Member 3): hash -> bucket; linear scan of the chain.
  Result<Value*> find(const Key& key) {
    (void)key;
    return Error::not_implemented("HashTable::find");
  }
  Result<const Value*> find(const Key& key) const {
    (void)key;
    return Error::not_implemented("HashTable::find");
  }

  /// Erases key. Returns true if something was removed.
  /// TODO(Member 3): hash -> bucket; find in chain; swap-with-last + pop_back; --size_.
  Result<bool> erase(const Key& key) {
    (void)key;
    return Error::not_implemented("HashTable::erase");
  }

  /// TODO(Member 3): find(key) != nullptr.
  Result<bool> contains(const Key& key) const {
    (void)key;
    return Error::not_implemented("HashTable::contains");
  }

  /// Rebuilds the table with at least `new_bucket_count` buckets.
  /// TODO(Member 3): allocate new bucket vector; re-insert every node by recomputing
  /// hash % new_count; swap into buckets_.
  Status rehash(std::size_t new_bucket_count) {
    (void)new_bucket_count;
    return Error::not_implemented("HashTable::rehash");
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] std::size_t bucket_count() const noexcept { return buckets_.size(); }
  [[nodiscard]] float load_factor() const noexcept {
    return static_cast<float>(size_) / static_cast<float>(buckets_.size());
  }
  [[nodiscard]] float max_load_factor() const noexcept { return max_load_factor_; }
  void set_max_load_factor(float f) noexcept { max_load_factor_ = f; }

  /// Length of the chain for `key`'s bucket — lets tests verify collision handling.
  [[nodiscard]] std::size_t chain_length(const Key& key) const {
    return buckets_[bucket_index(key)].size();
  }

  /// Debug helper: sum of chain lengths == size_ and every node sits in the right bucket.
  [[nodiscard]] bool check_invariants() const {
    std::size_t counted = 0;
    for (std::size_t b = 0; b < buckets_.size(); ++b) {
      for (const Node& n : buckets_[b]) {
        if (bucket_index(n.key) != b)
          return false;
        ++counted;
      }
    }
    return counted == size_;
  }

 private:
  struct Node {
    Key key;
    Value value;
  };

  [[nodiscard]] std::size_t bucket_index(const Key& key) const {
    return hasher_(key) % buckets_.size();
  }

  std::vector<std::vector<Node>> buckets_;
  std::size_t size_{0};
  float max_load_factor_{kDefaultMaxLoadFactor};
  Hash hasher_{};
  KeyEqual equal_{};
};

}  // namespace aegis::ds
