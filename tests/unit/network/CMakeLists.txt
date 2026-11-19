# Member 1 — Core Networking
aegis_add_test(test_socket_handle            test_socket_handle.cpp            member1 aegis_network)
aegis_add_test(test_buffer                   test_buffer.cpp                   member1 aegis_network)
aegis_add_test(test_event_loop               test_event_loop.cpp               member1 aegis_network)
aegis_add_test(test_connection_state_machine test_connection_state_machine.cpp member1 aegis_network)
aegis_add_test(test_backpressure             test_backpressure.cpp             member1 aegis_network)
