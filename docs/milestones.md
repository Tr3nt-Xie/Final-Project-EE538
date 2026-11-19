# Six-week plan

Each week ends with `main` green. Integration checkpoints (end of weeks 2, 4, 6) are
half-day sessions where all three rebase, run `scripts/run-local.sh` together and fix
cross-module issues before new work starts.

| Week | Member 1 — Network (TL) | Member 2 — HTTP & Proxy | Member 3 — Balancing & Reliability |
|---|---|---|---|
| **1** | Read `docs/ownership.md`; review this framework; implement `BackpressureController::evaluate` (+ enable tests); add `EventLoop` timers (`timerfd`, `add_timer(interval, cb)`) | Implement `HttpParser` request line + headers (+ enable `ParsesSimpleGet`, `RejectsMalformedRequestLine`, `RejectsOversizedHeaders`); define `ProxySession` fields | Implement `IndexedMinHeap` (`insert/peek_min/extract_min/sift_*`) and `HashTable` (`insert/find/erase`), enable their tests |
| **2** | Hook backpressure into `ConnectionManager` (pause/resume Readable); idle-timeout on connections; `ConnectionManager` helper to register/unregister backend fds | `HttpParser` fixed-length body + partial input (+ enable `HandlesPartialInputAcrossFeeds`); `RequestForwarder::serialize_for_backend` | `IndexedMinHeap::remove/update_key`, `HashTable::rehash` + load factor; `RoundRobinBalancer::select` (+ tests); `HealthMonitor::record_probe_result` transitions (+ tests) |
| **Checkpoint A** | *Proxy accepts a request, parses it, selects a backend by round-robin, and returns 502 "forwarding not implemented" with the chosen backend's name in the body.* | | |
| **3** | Edge-triggered epoll behind a flag; benchmark LT vs ET with `wrk` against a 200-OK stub; graceful shutdown drains in-flight connections | `RequestForwarder::forward`: connect via `BackendConnector`, send request, relay response bytes to `client.outbound()`; `on_connection_started/finished` reporting | `LeastConnectionsBalancer` on top of the heap (+ tests); `HealthMonitor::tick` scheduling; health probe helper (GET `/health` over the loop) |
| **4** | Connection limits & per-backend concurrency caps; `Connection` stats; profile with `perf` and remove hot-path allocations | Keep-alive on the client side (`parser.reset()`, state back to `ReadingRequest`); chunked request bodies (+ enable `ParsesChunkedBody`); 503/504 paths | Automatic failover: retry policy in `RequestForwarder` agreed with Member 2; `Draining` state; `AdaptiveBalancer` scoring + EWMA |
| **Checkpoint B** | *End-to-end: `curl` through the proxy to two Python backends works with all three strategies; killing one backend fails over within one health interval; ASan clean.* | | |
| **5** | Backpressure under a slow client (`wrk` + `tc`/socat throttling) and large responses; fix buffer growth policy; CI adds a Release build with `-O2` benchmarks | Response parsing for `Content-Length`/chunked so keep-alive to backends is possible; `X-Forwarded-For`/`Via`; request/response size limits | Hostname resolution at config load; `HashTable` used for `BackendRegistry` name lookups; health-check jitter; metrics counters per backend |
| **6** | Final benchmark matrix (strategy × LT/ET × connections), write `benchmarks/README.md` results; release tag | Robustness: fuzz the parser (`libFuzzer` harness or random-split tests); documentation of HTTP edge cases handled | Final report sections on data structures (complexity proofs, measurements) and adaptive balancing results |
| **Checkpoint C** | *Demo + report. All `DISABLED_` tests either enabled or deleted with justification.* | | |

## Definition of done per feature

1. Behaviour implemented; the corresponding `DISABLED_` tests enabled and passing.
2. No `NotImplemented` left on that path; error cases return meaningful codes.
3. `asan` preset green; `scripts/format.sh --check` and `scripts/lint.sh` clean.
4. Docs updated if an interface or ownership rule changed.
5. Reviewed and merged to `main` through a PR.

## Dependencies between members (plan around these)

* Member 2's `RequestForwarder::forward` (week 3) needs Member 1's backend-fd helper
  (week 2) and timers for connect timeouts (week 1).
* Member 3's `LeastConnectionsBalancer` (week 3) needs `IndexedMinHeap` (weeks 1–2) and the
  `on_connection_started/finished` calls from Member 2's forwarder (week 3) to be useful
  end to end — unit tests can drive the registry directly before that.
* Health probing (week 3) needs timers (Member 1, week 1); until then
  `record_probe_result` is testable in isolation.
* Failover (week 4) is a joint design between Members 2 and 3: agree on the retry
  interface at Checkpoint A.
