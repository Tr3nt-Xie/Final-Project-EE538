add_library(aegis_http STATIC
  http_message.cpp
  http_parser.cpp)
target_link_libraries(aegis_http PUBLIC aegis_options aegis_common)
