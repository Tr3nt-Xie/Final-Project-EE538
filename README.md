# AegisProxy

*EE 538 final project (Final-Project-EE538).*

A C++20, epoll-based HTTP/1.1 reverse proxy and load balancer. Three-person university
systems project. **This repository is currently a validated framework, not a working
proxy**: the build, tests, CI, module boundaries and interfaces exist; the HTTP parser,
forwarding, custom data-structure algorithms, scheduling and health logic are documented
`TODO`s that return `ErrorCode::NotImplemented`.

```
Client ──▶ EventLoop ──▶ ConnectionManager ──▶ HttpParser ──▶ LoadBalancer ──▶ BackendConnector ──▶ Backend
   ◀──────────────────────────────── response relayed back ◀───────────────────────────────┘
```

## Build and test (Linux)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/aegisproxy --config config/aegisproxy.example.conf
```

Presets: `cmake --preset debug|release|asan|ci` then `cmake --build --preset <name>` and
`ctest --preset <name>`. Requires CMake ≥ 3.20, GCC ≥ 13 or Clang ≥ 17, GoogleTest
(`apt install libgtest-dev`, or it is downloaded automatically).

**macOS / Windows:** everything except `EpollPoller`, the integration tests and a
runnable `aegisproxy` builds and tests natively (Members 2 and 3 can work there). For
the full Linux build use Docker: `scripts/docker-dev.sh [debug|asan|release]`.

## Layout

| Path | Owner | Contents |
|---|---|---|
| `include/aegisproxy/common`, `src/common` | shared (TL reviews) | `Result<T>`/`Error`, `types.hpp`, logger, config parser |
| `include/aegisproxy/network`, `src/network` | Member 1 | `SocketHandle`, `Buffer`, `Poller`/`EpollPoller`, `EventLoop`, `Connection`, `ConnectionManager`, `ConnectionStateMachine`, `BackpressureController` |
| `include/aegisproxy/http`, `src/http` | Member 2 | `HttpRequest`, `HttpResponse`, `HttpParser` |
| `include/aegisproxy/proxy`, `src/proxy` | Member 2 | `HttpProxy`, `RequestForwarder`, `BackendConnector` |
| `include/aegisproxy/datastructures` | Member 3 | `IndexedMinHeap<K,P>`, `HashTable<K,V>` (header-only templates) |
| `include/aegisproxy/balancer`, `src/balancer` | Member 3 | `BackendRegistry`, `LoadBalancingStrategy`, `RoundRobin`/`LeastConnections`/`Adaptive` balancers |
| `include/aegisproxy/health`, `src/health` | Member 3 | `HealthMonitor` |
| `tests/unit`, `tests/integration` | each owner | GoogleTest; `DISABLED_` tests document pending behaviour |
| `docs/` | all | architecture, interfaces, ownership, development, milestones |
| `examples/backend_server` | — | Python test backend |
| `benchmarks/` | — | micro-benchmark harness (no results yet) |
| `scripts/` | — | Docker dev image, format, lint, local run |

## Documentation

* [docs/architecture.md](docs/architecture.md) — pipeline, module boundaries, request lifecycle
* [docs/interfaces.md](docs/interfaces.md) — public interfaces and cross-module contracts
* [docs/ownership.md](docs/ownership.md) — who owns which resource; stale events and fd reuse
* [docs/development.md](docs/development.md) — build, test, format, debug, Docker
* [docs/milestones.md](docs/milestones.md) — six-week plan with integration checkpoints
* [CONTRIBUTING.md](CONTRIBUTING.md) — branches, commits, PRs, review checklist

## Status at a glance

Implemented and tested: `Result`/`Error`, config parsing, `SocketHandle`, `Buffer`,
`Poller`/`EpollPoller`, `EventLoop` (registration, dispatch, stale-event dropping,
deferred tasks, wake-up), `ConnectionStateMachine`, `Connection` I/O, `ConnectionManager`
(accept, read, write-interest, two-phase close, limits), HTTP message helpers and
response serialisation, `BackendRegistry` (health/load fan-out), strategy factory,
`HealthMonitor` lifecycle, application startup/shutdown via `signalfd`, CI.

Not implemented (explicit `NotImplemented` errors, `DISABLED_` tests): `HttpParser::feed`,
request forwarding, `IndexedMinHeap`/`HashTable` algorithms, all three `select()`
methods, health probing and state transitions, failover, backpressure decisions, EventLoop
timers, hostname resolution, edge-triggered epoll, benchmarks.
