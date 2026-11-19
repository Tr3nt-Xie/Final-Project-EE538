#include "aegisproxy/network/backpressure_controller.hpp"

namespace aegis::net {

BackpressureController::BackpressureController(Thresholds thresholds) : thresholds_(thresholds) {}

Result<BackpressureController::Decision> BackpressureController::evaluate(std::size_t queued_bytes,
                                                                          bool reads_paused) const {
  (void)queued_bytes;
  (void)reads_paused;
  return Error::not_implemented("BackpressureController::evaluate");
}

}  // namespace aegis::net
