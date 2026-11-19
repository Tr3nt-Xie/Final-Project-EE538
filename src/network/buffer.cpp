#include "aegisproxy/network/buffer.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace aegis::net {

Buffer::Buffer(std::size_t initial_capacity, std::size_t max_capacity)
    : storage_(std::min(initial_capacity, max_capacity)), max_capacity_(max_capacity) {}

std::span<const std::byte> Buffer::readable() const noexcept {
  return {storage_.data() + read_pos_, readable_bytes()};
}

std::string_view Buffer::view() const noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): byte -> char view
  return {reinterpret_cast<const char*>(storage_.data() + read_pos_), readable_bytes()};
}

void Buffer::compact() noexcept {
  if (read_pos_ == 0)
    return;
  const std::size_t n = readable_bytes();
  if (n > 0) {
    std::memmove(storage_.data(), storage_.data() + read_pos_, n);
  }
  read_pos_ = 0;
  write_pos_ = n;
}

Result<std::span<std::byte>> Buffer::prepare(std::size_t min_bytes) {
  if (min_bytes == 0)
    min_bytes = 1;
  if (storage_.size() - write_pos_ < min_bytes) {
    compact();
  }
  if (storage_.size() - write_pos_ < min_bytes) {
    const std::size_t needed = write_pos_ + min_bytes;
    if (needed > max_capacity_) {
      return Error{ErrorCode::CapacityExceeded, "buffer would exceed max capacity of " +
                                                    std::to_string(max_capacity_) + " bytes"};
    }
    std::size_t new_size = std::max<std::size_t>(storage_.size() * 2, needed);
    new_size = std::min(new_size, max_capacity_);
    storage_.resize(new_size);
  }
  return std::span<std::byte>{storage_.data() + write_pos_, storage_.size() - write_pos_};
}

void Buffer::commit(std::size_t n) noexcept {
  write_pos_ = std::min(write_pos_ + n, storage_.size());
}

void Buffer::consume(std::size_t n) noexcept {
  read_pos_ = std::min(read_pos_ + n, write_pos_);
  if (read_pos_ == write_pos_) {
    read_pos_ = write_pos_ = 0;
  }
}

Status Buffer::append(std::span<const std::byte> data) {
  if (data.empty())
    return ok();
  auto region = prepare(data.size());
  if (!region)
    return region.error();
  std::memcpy(region->data(), data.data(), data.size());
  commit(data.size());
  return ok();
}

Status Buffer::append(std::string_view text) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return append(
      std::span<const std::byte>{reinterpret_cast<const std::byte*>(text.data()), text.size()});
}

void Buffer::clear() noexcept {
  read_pos_ = write_pos_ = 0;
}

}  // namespace aegis::net
