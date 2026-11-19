#pragma once

#include "aegisproxy/common/result.hpp"
#include "aegisproxy/common/types.hpp"

#include <cstddef>
#include <cstdint>

namespace aegis::net {

/// Decides when to stop/resume reading from one side of a proxied connection so that a
/// slow consumer cannot make the proxy buffer unboundedly.
///
/// Intended wiring (Member 1 + Member 2):
///   * Each Connection's outbound() has a bound (ConnectionLimits::buffer_limit_bytes).
///   * When client.outbound() crosses high_watermark, PauseReads on the *backend* socket
///     (stop pulling the response); when it drains below low_watermark, ResumeReads.
///   * Symmetrically for request bodies flowing client → backend.
///
/// Thread-safety: none.
class BackpressureController {
 public:
  struct Thresholds {
    std::size_t high_watermark_bytes{48 * 1024};
    std::size_t low_watermark_bytes{16 * 1024};
  };

  enum class Decision : std::uint8_t { Continue, PauseReads, ResumeReads };

  explicit BackpressureController(Thresholds thresholds);

  [[nodiscard]] const Thresholds& thresholds() const noexcept { return thresholds_; }

  /// Given the number of bytes queued for a peer and whether reads are currently
  /// paused, returns what the caller should do.
  /// TODO(Member 1): hysteresis — PauseReads when queued >= high and not paused;
  /// ResumeReads when queued <= low and paused; Continue otherwise.
  Result<Decision> evaluate(std::size_t queued_bytes, bool reads_paused) const;

 private:
  Thresholds thresholds_;
};

}  // namespace aegis::net
