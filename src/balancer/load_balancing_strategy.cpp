#include "aegisproxy/balancer/load_balancing_strategy.hpp"

namespace aegis::lb {

LoadBalancingStrategy::LoadBalancingStrategy(BackendRegistry& registry) : registry_(registry) {
  registry_.add_observer(*this);
}

LoadBalancingStrategy::~LoadBalancingStrategy() {
  registry_.remove_observer(*this);
}

}  // namespace aegis::lb
