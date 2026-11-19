# Member 3 — Load balancing
aegis_add_test(test_backend_registry test_backend_registry.cpp member3 aegis_balancer)
aegis_add_test(test_balancers        test_balancers.cpp        member3 aegis_balancer)
