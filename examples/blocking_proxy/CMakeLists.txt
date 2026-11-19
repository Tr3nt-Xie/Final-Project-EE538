# Member 2 — blocking-I/O reverse-proxy baseline for performance comparison.
# Deliberately independent of the aegis_network event loop: it is the "before" in the
# before/after benchmark. Links only the portable http module and common.
add_executable(blocking_proxy_baseline blocking_proxy.cpp)
target_link_libraries(blocking_proxy_baseline PRIVATE aegis_options aegis_common aegis_http)
