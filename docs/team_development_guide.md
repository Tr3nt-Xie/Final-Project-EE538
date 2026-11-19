# Team Development Guide

The one page each member needs to start work on day one. Details live in the linked docs.

## 1. Files owned by each member

| Member | Owns | Reviews |
|---|---|---|
| **1 — Technical Lead / Networking** | `include/aegisproxy/network/`, `src/network/`, `src/main.cpp`, `tests/unit/network/`, `tests/integration/network/`, `docs/architecture.md`, `docs/ownership.md`, `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `.github/`, `scripts/` | everything under `common/`, `docs/`, any public header change |
| **2 — HTTP & Proxy** | `include/aegisproxy/http/`, `include/aegisproxy/proxy/`, `src/http/`, `src/proxy/`, `tests/unit/http/`, `tests/integration/proxy/`, `examples/backend_server/`, `examples/blocking_proxy/` | Member 3's PRs |
| **3 — Balancing & Reliability** | `include/aegisproxy/datastructures/`, `include/aegisproxy/balancer/`, `include/aegisproxy/health/`, `src/datastructures/`, `src/balancer/`, `src/health/`, `tests/unit/datastructures/`, `tests/unit/balancer/`, `tests/unit/health/`, `tests/integration/failover/`, `benchmarks/` | Member 2's PRs |

`.github/CODEOWNERS` encodes this (replace `@member1/2/3` with real handles).
`scripts/todos.sh <1|2|3>` lists every `TODO(Member N)` in the code.

## 2. Exact classes each member must implement

**Member 1** (`aegis::net`)
- `ConnectionManager::adopt_backend`, `pause_reads`, `resume_reads` — currently `NotImplemented`
- `BackpressureController::evaluate` — hysteresis on high/low watermarks
- `EventLoop` timers (`timerfd`): needed by health probes, connect and idle timeouts
- Backend-connect completion in `ConnectionManager::on_connection_ready` (SO_ERROR check)
- Later: edge-triggered `EpollPoller`, `NetworkStats.bytes_in/out`, hostname resolution in `socket_ops`, profiling

**Member 2** (`aegis::http`, `aegis::proxy`)
- `HttpParser::feed` (request line → headers → body; `Content-Length`, later chunked) and `take_request`
- `RequestForwarder::serialize_for_backend`, `RequestForwarder::forward`
- `HttpProxy`: the real `on_client_data` pipeline, `on_backend_data`, writable/close handlers, 400/502/503/504 responses, keep-alive (`parser.reset()`)
- `ProxySession` fields (private to `http_proxy.cpp`)
- `examples/blocking_proxy/blocking_proxy.cpp` baseline
- Later: response parsing for backend keep-alive, `X-Forwarded-For`, parser fuzzing

**Member 3** (`aegis::ds`, `aegis::lb`, `aegis::health`)
- `ds::IndexedMinHeap`: `insert`, `remove`, `update_key`, `peek_min`, `extract_min`, `contains` + `sift_up/sift_down`
- `ds::HashTable`: `insert`, `find`, `erase`, `contains`, `rehash` (+ load-factor growth)
- `RoundRobinBalancer::select` (+ `on_health_changed`)
- `LeastConnectionsBalancer`: all five overrides on top of the heap
- `AdaptiveBalancer`: EWMA update + score + heap
- `HealthMonitor::tick`, `HealthMonitor::record_probe_result`; a probe helper driven from the loop (with Member 1's timers)
- Failover retry policy (joint with Member 2), failure-injection tests, `benchmarks/bench_datastructures.cpp` results

## 3. Dependencies between members

```
M1 timers (wk1) ──────────────┬──▶ M3 health probing (wk3)
M1 adopt_backend (wk1) ───────┼──▶ M2 forward() (wk2–3)
M1 pause/resume_reads (wk2) ──┴──▶ M2 backpressure wiring (wk5)
M3 IndexedMinHeap (wk1–2) ────────▶ M3 LeastConnections (wk3), Adaptive (wk4)
M2 forward() reports on_connection_started/finished ──▶ M3 LeastConnections meaningful end to end (wk3)
M3 strategies select() ───────────▶ M2 proxy picks a real backend (Checkpoint A)
M2 + M3 agree failover retry interface ───▶ both implement their half (wk4)
```

Until a dependency lands, code against the interface and keep the `DISABLED_` test; never
fork another member's module.

## 4. Week-by-week milestones

See `docs/milestones.md` (full table). Summary:

| Week | M1 | M2 | M3 |
|---|---|---|---|
| 1 | `adopt_backend`, timers, `evaluate` | parser: request line + headers | heap core, hash table core |
| 2 | `pause/resume_reads`, idle timeout, hook backpressure | parser: body + partial input; `serialize_for_backend` | heap `remove/update_key`, `rehash`; round robin; health transitions |
| **A** | *request parsed → backend selected by RR → 502 "forwarding not implemented" naming the backend* | | |
| 3 | edge-triggered flag + LT/ET benchmark; graceful drain | `forward()` end to end; completion reporting | least connections; `tick` + probe helper |
| 4 | per-backend caps; profiling; hot-path allocations | keep-alive; chunked bodies; 503/504 | failover policy (with M2); Draining; adaptive |
| **B** | *`curl` through the proxy to two backends with all strategies; kill one → failover within one interval; ASan clean* | | |
| 5 | slow-client backpressure tests; Release CI | backend keep-alive; `X-Forwarded-For`; size limits; blocking baseline numbers | hostname resolution; `HashTable` in registry; jitter; metrics |
| 6 | benchmark matrix; tag | parser fuzzing; HTTP edge-case docs | complexity analysis; adaptive results |
| **C** | *demo + report; every `DISABLED_` test enabled or removed with justification* | | |

## 5. Integration checkpoints

| Checkpoint | When | Everyone does | Exit criterion |
|---|---|---|---|
| **A** | end of week 2 | rebase on `main`; `scripts/run-local.sh`; `curl -i :8080/` | 502 body names the backend chosen by round robin; all enabled tests green under `asan` |
| **B** | end of week 4 | same + kill one backend during `wrk` | traffic moves within one health interval; no ASan/UBSan reports; `ctest` green on gcc + clang |
| **C** | end of week 6 | demo rehearsal | benchmark table filled from reproducible runs; docs match code |

## 6. Acceptance criteria per module

| Module | Accepted when |
|---|---|
| `network` | `ctest -L member1` green including the three `DISABLED_` lifecycle tests re-enabled; `asan` preset green; 1k concurrent idle connections hold without growth in RSS; backpressure test with a throttled client bounds memory |
| `http` | `ctest -L member2` unit tests green incl. partial-input and oversized-header tests; parser never reads past `bytes_consumed`; malformed input → `ParseError`, never UB (fuzz 10 min clean) |
| `proxy` | `tests/integration/proxy` green: GET/POST relayed byte-exact, 400/502/503 paths, completion reported to the registry; `scripts/run-local.sh` + `curl` works; baseline binary produces numbers |
| `datastructures` | `ctest -L member3` heap + hash-table tests green incl. randomized `check_invariants()`; complexity analysis in the report matches measured growth from `bench_datastructures` |
| `balancer` | all three `select()` tests green; adding a fourth strategy needs no change outside `balancer/` |
| `health` | transition tests green; probing runs on the loop; `tests/integration/failover` green |

## 7. Commands — build and test one module independently

```bash
# Configure once (Linux; macOS builds everything except epoll/integration)
cmake --preset debug            # or: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build only your library and tests
cmake --build build/debug --target aegis_network test_event_loop test_connection_lifecycle   # M1
cmake --build build/debug --target aegis_http aegis_proxy test_http_parser test_http_proxy    # M2
cmake --build build/debug --target aegis_balancer aegis_health test_indexed_min_heap \
                                   test_hash_table test_balancers test_health_monitor          # M3

# Run only your tests (labels: member1 | member2 | member3 | shared)
ctest --test-dir build/debug -L member1 --output-on-failure
ctest --test-dir build/debug -L member2 --output-on-failure
ctest --test-dir build/debug -L member3 --output-on-failure

# Run a disabled test on purpose while you implement it
./build/debug/tests/unit/datastructures/test_indexed_min_heap --gtest_also_run_disabled_tests \
    --gtest_filter='IndexedMinHeap.*'

# Everything, as CI does it
cmake --workflow --preset ci

# macOS / Windows: full Linux build in Docker
scripts/docker-dev.sh asan

# Format, lint, list your TODOs
scripts/format.sh && scripts/lint.sh build/debug && scripts/todos.sh 2
```
