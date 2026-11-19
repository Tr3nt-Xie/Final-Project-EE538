# Member 2 — end-to-end HTTP through the proxy
aegis_add_test(test_http_proxy test_http_proxy.cpp member2
               aegis_proxy aegis_network aegis_http aegis_balancer)
