add_library(aegis_network STATIC
  socket_handle.cpp
  buffer.cpp
  event_loop.cpp
  connection_state_machine.cpp
  connection.cpp
  connection_manager.cpp
  backpressure_controller.cpp)

if(AEGIS_HAVE_EPOLL)
  target_sources(aegis_network PRIVATE epoll_poller.cpp)
endif()

target_link_libraries(aegis_network PUBLIC aegis_options aegis_common)
