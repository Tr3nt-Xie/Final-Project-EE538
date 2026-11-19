# Header-only templates; this INTERFACE target exists so that dependents can link it
# and so that the headers show up in IDEs.
add_library(aegis_datastructures INTERFACE)
target_link_libraries(aegis_datastructures INTERFACE aegis_options aegis_common)
target_sources(aegis_datastructures INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/aegisproxy/datastructures/indexed_min_heap.hpp
    ${PROJECT_SOURCE_DIR}/include/aegisproxy/datastructures/hash_table.hpp)
