# Member 3 — health checks + failover through the proxy
aegis_add_test(test_failover test_failover.cpp member3
               aegis_proxy aegis_network aegis_http aegis_balancer aegis_health)
