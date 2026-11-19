add_library(aegis_proxy STATIC
  backend_connector.cpp
  request_forwarder.cpp
  http_proxy.cpp)
target_link_libraries(aegis_proxy PUBLIC
  aegis_options aegis_common aegis_network aegis_http aegis_balancer)
