add_library(aegis_balancer STATIC
  backend.cpp
  backend_registry.cpp
  load_balancing_strategy.cpp
  round_robin_balancer.cpp
  least_connections_balancer.cpp
  adaptive_balancer.cpp
  strategy_factory.cpp)
target_link_libraries(aegis_balancer PUBLIC aegis_options aegis_common aegis_datastructures)
