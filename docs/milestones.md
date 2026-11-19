# Six-week plan

Each week ends with `main` green. Integration checkpoints (end of weeks 2, 4, 6) are
half-day sessions where all three rebase, run `scripts/run-local.sh` together and fix
cross-module issues before new work starts. Priorities follow
`docs/team_responsibilities.md`: P1 (MVP) before P2, P3 only after Checkpoint B.

| Week | Member 1 — Network (TL) | Member 2 — HTTP & Proxy | Member 3 — Balancing & Reliability |
|---|---|---|---|
| **1** | `ConnectionManager::adopt_backend` + connect completion (enable `DISABLED_BackendConnect*` tests); `EventLoop` timers (`timerfd`, `add_timer`); `BackpressureController::evaluate` (enable its tests) | `HttpParser::feed` request line + headers (enable `ParsesSimpleGet`, `RejectsMalformedRequestLine`, `RejectsOversizedHeaders`); define `ProxySession` fields | `IndexedMinHeap` `insert/peek_min/extract_min` + sift helpers; `HashTable` `insert/find/erase` (enable their tests) |
| **2** | `pause_reads/resume_reads` (enable `DISABLED_PauseReads*`); idle timeout per connection; wire `CapacityExceeded` → `pause_reads` instead of close | `Content-Length` body + partial input (enable `HandlesPartialInputAcrossFeeds`); `serialize_for_backend`; `HttpProxy` selects a backend and answers 502 naming it | `IndexedMinHeap` `remove/update_key`, `HashTable` `rehash` + growth; `RoundRobinBalancer::select` (enable tests); `HealthMonitor::record_probe_result` transitions (enable tests) |
| **Checkpoint A** | *Request parsed → backend chosen by round robin → 502 "forwarding not implemented" with the backend name in the body. `ctest` green under `asan`.* | | |
| **3** | Edge-triggered `EpollPoller` behind a flag; LT vs ET `wrk` comparison against a 200-OK stub; graceful shutdown drains in-flight connections | `RequestForwarder::forward`: `adopt_backend` → send → relay response to client `outbound()`; `on_connection_started/finished` with latency (enable `ForwardsGetAndRelaysResponse`, `MalformedRequestGets400`, `BackendDownGets502`) | `LeastConnectionsBalancer` on the heap (enable tests); `HealthMonitor::tick` scheduling + probe helper on Member 1's timers (enable `TickReturnsDueBackends`) |
| **4** | Per-backend concurrency caps; `NetworkStats.bytes_in/out`; `perf` profile, remove hot-path allocations | Client keep-alive (`parser.reset()`, back to `ReadingRequest`); chunked request bodies (enable `ParsesChunkedBody`); 503/504 paths (enable `NoHealthyBackendGets503`) | Failover retry policy with Member 2 (`SelectionContext` skip list); `Draining`; `AdaptiveBalancer` EWMA + score (enable its test); enable `tests/integration/failover` |
| **Checkpoint B** | *`curl` through the proxy to two backends with all three strategies; kill one backend → traffic moves within one health interval; ASan/UBSan clean on gcc and clang.* | | |
| **5** | Backpressure under a slow client (`wrk` + throttling) and large responses; buffer growth policy; Release CI job | Response parsing for backend keep-alive; `X-Forwarded-For`/`Via`; request/response size limits; **blocking baseline numbers** vs AegisProxy | Hostname resolution at config load (with M1); `HashTable` for registry name lookups; probe jitter; per-backend metrics |
| **6** | Final benchmark matrix (strategy × LT/ET × connections) in `benchmarks/README.md`; release tag | Parser fuzz harness or random-split tests; HTTP edge-case documentation | Complexity analysis with measured growth from `bench_datastructures`; adaptive balancing results; fault-recovery benchmark |
| **Checkpoint C** | *Demo + report. Every `DISABLED_` test enabled or deleted with justification.* | | |

## Definition of done per feature

1. Behaviour implemented; the corresponding `DISABLED_` tests enabled and passing.
2. No `NotImplemented` left on that path; error cases return meaningful codes.
3. `asan` preset green; `scripts/format.sh --check` and `scripts/lint.sh` clean.
4. Docs updated if an interface or ownership rule changed.
5. Reviewed and merged to `main` through a PR.

## Dependencies between members (plan around these)

* Member 2's `forward()` (week 3) needs Member 1's `adopt_backend` (week 1) and timers
  for connect timeouts (week 1).
* Member 3's `LeastConnectionsBalancer` (week 3) needs `IndexedMinHeap` (weeks 1–2) and
  Member 2's `on_connection_started/finished` calls (week 3) to matter end to end — unit
  tests drive the registry directly before that.
* Health probing (week 3) needs timers (Member 1, week 1); `record_probe_result` is
  testable in isolation from week 2.
* Failover (week 4) is a joint design between Members 2 and 3: agree the retry count and
  the "skip these ids" mechanism at Checkpoint A.
* Backpressure wiring in the proxy (week 5, Member 2) needs `pause_reads/resume_reads`
  (Member 1, week 2) and `evaluate` (week 1).
