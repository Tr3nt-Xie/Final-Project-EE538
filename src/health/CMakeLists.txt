add_library(aegis_health STATIC
  health_monitor.cpp)
target_link_libraries(aegis_health PUBLIC aegis_options aegis_common aegis_balancer)
