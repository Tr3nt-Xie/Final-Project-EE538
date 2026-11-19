# Blocking-I/O proxy baseline (Member 2)

A deliberately naive reverse proxy — blocking sockets, sequential or thread-per-connection
— used only as the performance baseline for AegisProxy's epoll design. It must not link
`aegis_network`.

```bash
cmake --build build --target blocking_proxy_baseline
./build/examples/blocking_proxy/blocking_proxy_baseline --listen 8081 --backend 127.0.0.1:9001
```

Status: skeleton (argument parsing only); exits 2 until implemented. The plan is in the
header comment of `blocking_proxy.cpp`; results go to `benchmarks/README.md`.
