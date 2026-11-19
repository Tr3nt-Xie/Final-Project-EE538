#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace aegis::net {

/// Readiness flags. Bit-combinable.
enum class EventMask : std::uint32_t {
  None = 0,
  Readable = 1U << 0,
  Writable = 1U << 1,
  Error = 1U << 2,   ///< Always reported, never needs to be requested.
  HangUp = 1U << 3,  ///< Always reported, never needs to be requested.
};

[[nodiscard]] constexpr EventMask operator|(EventMask a, EventMask b) noexcept {
  return static_cast<EventMask>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr EventMask operator&(EventMask a, EventMask b) noexcept {
  return static_cast<EventMask>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr EventMask operator~(EventMask a) noexcept {
  return static_cast<EventMask>(~static_cast<std::uint32_t>(a));
}
constexpr EventMask& operator|=(EventMask& a, EventMask b) noexcept {
  return a = a | b;
}
constexpr EventMask& operator&=(EventMask& a, EventMask b) noexcept {
  return a = a & b;
}
[[nodiscard]] constexpr bool has(EventMask mask, EventMask flag) noexcept {
  return (mask & flag) != EventMask::None;
}

/// One readiness notification. `token` is the HandlerId the fd was registered with —
/// the loop dispatches on the token, never on the fd (see docs/ownership.md, "Stale
/// events and fd reuse").
struct PollEvent {
  HandlerId token{kInvalidHandlerId};
  EventMask events{EventMask::None};
};

/// Abstract readiness poller. EpollPoller is the production implementation (Linux,
/// level-triggered). Tests may supply a fake to drive EventLoop deterministically.
///
/// Thread-safety: none; owned and called by exactly one EventLoop.
class Poller {
 public:
  virtual ~Poller() = default;
  Poller(const Poller&) = delete;
  Poller& operator=(const Poller&) = delete;

  virtual Status add(Fd fd, EventMask interest, HandlerId token) = 0;
  virtual Status modify(Fd fd, EventMask interest, HandlerId token) = 0;
  virtual Status remove(Fd fd) = 0;

  /// Blocks up to `timeout` (negative = forever) and fills `out`. Returns the number of
  /// events written. EINTR is reported as 0 events, not as an error.
  virtual Result<std::size_t> wait(std::span<PollEvent> out, std::chrono::milliseconds timeout) = 0;

 protected:
  Poller() = default;
};

}  // namespace aegis::net
