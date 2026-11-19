add_library(aegis_common STATIC
  error.cpp
  log.cpp
  config.cpp)
target_link_libraries(aegis_common PUBLIC aegis_options)
