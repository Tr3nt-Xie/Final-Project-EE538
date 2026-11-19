#pragma once

#include "aegisproxy/common/result.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace aegis::net {

/// Bounded, growable byte buffer with a read cursor and a write cursor:
///
///   [ consumed | readable (read_pos_..write_pos_) | writable | unused up to max ]
///
/// Typical I/O cycle:
///   auto span = buf.prepare(4096);   // writable region (grows if needed, up to max)
///   n = recv(fd, span.data(), span.size());
///   buf.commit(n);                   // make the bytes readable
///   ... parser consumes buf.readable() ...
///   buf.consume(parsed_bytes);
///
/// `max_capacity` is the bound that BackpressureController reasons about: prepare()
/// fails with CapacityExceeded rather than growing past it.
///
/// Owner: Member 1. Each Connection owns two Buffers (inbound, outbound).
/// Thread-safety: none.
class Buffer {
 public:
  static constexpr std::size_t kDefaultInitialCapacity = 4096;
  static constexpr std::size_t kDefaultMaxCapacity = 1U << 20;  // 1 MiB

  explicit Buffer(std::size_t initial_capacity = kDefaultInitialCapacity,
                  std::size_t max_capacity = kDefaultMaxCapacity);

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&&) noexcept = default;
  Buffer& operator=(Buffer&&) noexcept = default;
  ~Buffer() = default;

  /// Bytes written but not yet consumed.
  [[nodiscard]] std::span<const std::byte> readable() const noexcept;
  [[nodiscard]] std::string_view view() const noexcept;
  [[nodiscard]] std::size_t readable_bytes() const noexcept { return write_pos_ - read_pos_; }
  [[nodiscard]] bool empty() const noexcept { return readable_bytes() == 0; }

  /// Returns a writable region of at least `min_bytes`, compacting or growing as needed.
  /// Error CapacityExceeded if that would exceed max_capacity().
  Result<std::span<std::byte>> prepare(std::size_t min_bytes);

  /// Marks `n` bytes of the region returned by prepare() as written.
  void commit(std::size_t n) noexcept;

  /// Discards `n` readable bytes from the front.
  void consume(std::size_t n) noexcept;

  /// Copies data in (prepare + memcpy + commit). Error CapacityExceeded if it won't fit.
  Status append(std::span<const std::byte> data);
  Status append(std::string_view text);

  void clear() noexcept;

  [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }
  [[nodiscard]] std::size_t max_capacity() const noexcept { return max_capacity_; }
  /// Headroom before the bound is hit (ignores already-consumed space, which compaction
  /// can reclaim).
  [[nodiscard]] std::size_t remaining_capacity() const noexcept {
    return max_capacity_ - readable_bytes();
  }

 private:
  void compact() noexcept;

  std::vector<std::byte> storage_;
  std::size_t read_pos_{0};
  std::size_t write_pos_{0};
  std::size_t max_capacity_;
};

}  // namespace aegis::net
