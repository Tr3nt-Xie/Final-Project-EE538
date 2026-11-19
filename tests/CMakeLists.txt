include(GoogleTest)      # CMake's gtest_discover_tests
include(FindOrFetchGTest)  # provides GTest::gtest(_main)

# aegis_add_test(<name> <source> <libs...>)
function(aegis_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE aegis_options ${ARGN} GTest::gtest GTest::gtest_main)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(${name}
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30)
endfunction()

# ---- unit (portable) ----
aegis_add_test(test_result                   unit/test_result.cpp                   aegis_common)
aegis_add_test(test_config                   unit/test_config.cpp                   aegis_common)
aegis_add_test(test_socket_handle            unit/test_socket_handle.cpp            aegis_network)
aegis_add_test(test_buffer                   unit/test_buffer.cpp                   aegis_network)
aegis_add_test(test_event_loop               unit/test_event_loop.cpp               aegis_network)
aegis_add_test(test_connection_state_machine unit/test_connection_state_machine.cpp aegis_network)
aegis_add_test(test_backpressure             unit/test_backpressure.cpp             aegis_network)
aegis_add_test(test_indexed_min_heap         unit/test_indexed_min_heap.cpp         aegis_datastructures)
aegis_add_test(test_hash_table               unit/test_hash_table.cpp               aegis_datastructures)
aegis_add_test(test_backend_registry         unit/test_backend_registry.cpp         aegis_balancer)
aegis_add_test(test_balancers                unit/test_balancers.cpp                aegis_balancer)
aegis_add_test(test_health_monitor           unit/test_health_monitor.cpp           aegis_health)
aegis_add_test(test_http_parser              unit/test_http_parser.cpp              aegis_http)

# ---- integration (needs epoll) ----
if(AEGIS_HAVE_EPOLL)
  aegis_add_test(test_connection_lifecycle integration/test_connection_lifecycle.cpp
                 aegis_network aegis_proxy aegis_balancer aegis_http)
endif()
