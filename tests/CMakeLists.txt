include(GoogleTest)        # CMake's gtest_discover_tests
include(FindOrFetchGTest)  # provides GTest::gtest(_main)

# aegis_add_test(<name> <source> <label> <libs...>)
#   <label> is the owning member: member1 | member2 | member3 | shared
#   Run one member's tests with:  ctest --test-dir build -L member2
function(aegis_add_test name source label)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE aegis_options ${ARGN} GTest::gtest GTest::gtest_main)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(${name}
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30 LABELS "${label}")
endfunction()

# One directory per module so members never edit the same test CMakeLists.
add_subdirectory(unit/common)          # shared
add_subdirectory(unit/network)         # Member 1
add_subdirectory(unit/http)            # Member 2
add_subdirectory(unit/datastructures)  # Member 3
add_subdirectory(unit/balancer)        # Member 3
add_subdirectory(unit/health)          # Member 3

if(AEGIS_HAVE_EPOLL)
  add_subdirectory(integration/network)   # Member 1
  add_subdirectory(integration/proxy)     # Member 2
  add_subdirectory(integration/failover)  # Member 3
endif()
