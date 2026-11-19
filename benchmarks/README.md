# Benchmarks

Nothing here has been measured yet. **Do not quote numbers that do not come from a
run you can reproduce with the commands below**, and record the hardware, compiler,
build type and the exact commit alongside every result.

## Micro-benchmarks (data structures)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAEGIS_BUILD_BENCHMARKS=ON
cmake --build build -j
./build/benchmarks/bench_datastructures
```

The binary exits with status 2 while `IndexedMinHeap` / `HashTable` are stubs.

## End-to-end throughput (later milestone)

Planned method, once forwarding works:

1. Start two `simple_backend.py` instances and the proxy (`scripts/run-local.sh`).
2. Baseline the backends directly with `wrk -t4 -c64 -d30s http://127.0.0.1:9001/`.
3. Measure through the proxy: `wrk -t4 -c64 -d30s http://127.0.0.1:8080/`.
4. Report requests/s, p50/p99 latency and proxy CPU (`perf stat` / `top`), for each
   strategy, with and without backpressure limits.
5. Compare level-triggered vs edge-triggered epoll once the latter exists.

Store raw `wrk` output under `benchmarks/results/<date>-<commit>/` and summarise in a
table in this file.
