# Team responsibilities

Three developers, three workstreams, one `main` that always compiles. Member 1 is the
Technical Lead and owns the architecture and integration; each member implements and
tests their own core functionality.

## Member 1 — Technical Lead & Core Networking Engineer

**Focus:** high-performance networking and systems architecture.

| # | Responsibility | Where |
|---|---|---|
| 1 | Overall architecture and cross-module interfaces | `docs/architecture.md`, `docs/interfaces.md`, all public headers (review) |
| 2 | Linux `epoll` event loop | `EventLoop`, `Poller`, `EpollPoller` |
| 3 | Non-blocking TCP socket operations | `SocketHandle`, `socket_ops` |
| 4 | Connection state machine | `ConnectionStateMachine` |
| 5 | RAII connection lifecycle management | `Connection`, `ConnectionManager` (incl. `adopt_backend`) |
| 6 | Partial reads/writes, socket errors, connection timeouts | `Connection::read_available/flush`, `EventLoop` timers (TODO) |
| 7 | Bounded buffers and backpressure (high/low-water marks) | `Buffer`, `BackpressureController`, `ConnectionManager::pause_reads/resume_reads` |
| 8 | Integration, profiling, optimisation | `src/main.cpp`, `benchmarks/` method, edge-triggered epoll |

**Owned paths:** `include/aegisproxy/network/`, `src/network/`, `src/main.cpp`,
`tests/unit/network/`, `tests/integration/network/`, `docs/architecture.md`,
`docs/ownership.md`, build system and CI.

**Deliverables:** working epoll event loop · non-blocking networking engine · connection
state machine · RAII connection management · backpressure mechanism · networking
performance analysis.

## Member 2 — HTTP Protocol & Proxy Engineer

**Focus:** HTTP processing and request forwarding.

| # | Responsibility | Where |
|---|---|---|
| 1 | Incremental HTTP/1.1 request (and response) parser | `HttpParser` |
| 2 | Headers and `Content-Length` bodies | `HttpParser`, `HttpRequest` |
| 3 | Client → backend request forwarding | `RequestForwarder`, `HttpProxy` |
| 4 | Backend connection establishment | `BackendConnector` + `ConnectionManager::adopt_backend` (provided by Member 1) |
| 5 | Backend → client response forwarding | `RequestForwarder`, `HttpProxy` handlers |
| 6 | Malformed requests, backend errors, incomplete messages | 400/502/503/504 paths in `HttpProxy` |
| 7 | Blocking-I/O proxy baseline | `examples/blocking_proxy/` |
| 8 | HTTP correctness and integration tests | `tests/unit/http/`, `tests/integration/proxy/` |

**Owned paths:** `include/aegisproxy/http/`, `include/aegisproxy/proxy/`, `src/http/`,
`src/proxy/`, `tests/unit/http/`, `tests/integration/proxy/`, `examples/backend_server/`,
`examples/blocking_proxy/`.

**Deliverables:** HTTP/1.1 parser · request/response models · functional reverse proxy ·
backend connector · blocking-I/O baseline · HTTP correctness tests.

## Member 3 — Load Balancing & Reliability Engineer

**Focus:** data structures, scheduling algorithms, fault tolerance.

| # | Responsibility | Where |
|---|---|---|
| 1 | Custom indexed binary min-heap | `ds::IndexedMinHeap` |
| 2 | Custom hash table | `ds::HashTable` |
| 3 | Round-robin scheduling | `RoundRobinBalancer` |
| 4 | Least-connections scheduling on the indexed heap | `LeastConnectionsBalancer` |
| 5 | Adaptive balancing (connections + EWMA latency) | `AdaptiveBalancer` |
| 6 | Backend registry and health-state tracking | `BackendRegistry` (done), `HealthState` |
| 7 | Periodic health checks and automatic failover | `HealthMonitor`, failover policy agreed with Member 2 |
| 8 | Data-structure tests, scheduling benchmarks, failure-injection tests | `tests/unit/datastructures|balancer|health/`, `tests/integration/failover/`, `benchmarks/` |

**Owned paths:** `include/aegisproxy/datastructures/`, `include/aegisproxy/balancer/`,
`include/aegisproxy/health/`, `src/datastructures/`, `src/balancer/`, `src/health/`,
`tests/unit/datastructures/`, `tests/unit/balancer/`, `tests/unit/health/`,
`tests/integration/failover/`, `benchmarks/`.

**Deliverables:** custom indexed min-heap · custom hash table · round-robin, least-
connections and adaptive balancers · backend health monitor · automatic failover ·
algorithm complexity analysis.

## Shared responsibilities

All three: pull requests and code reviews (`CONTRIBUTING.md`), cross-module integration,
unit and integration testing, debugging interface incompatibilities, performance
benchmarking, final documentation, final presentation and demo.

Shared code (`include/aegisproxy/common/`, `src/common/`, `tests/unit/common/`,
`tests/integration/proxy_test_support.hpp`, `docs/`) may be edited by anyone; the
Technical Lead reviews those PRs.

## Priorities

| Priority | Scope | Owner(s) |
|---|---|---|
| **P1 — required MVP** | compilable framework ✅ · epoll event loop ✅ · functional HTTP proxy · custom heap + hash table · round robin + least connections · basic health checks + failover · correctness tests | all |
| **P2 — resume-enhancing** | state-machine improvements · bounded-buffer backpressure · adaptive balancing · performance profiling · fault-recovery benchmarks | M1 · M1 · M3 · M1 · M3 |
| **P3 — optional** | keep-alive improvements · multithreaded event loops · observability · AWS EC2 deployment | M2 · M1 · all · all |

Do not start P3 before P1 works end to end.

See `docs/team_development_guide.md` for exact classes, acceptance criteria and commands.
