# Interfaces

All public headers live under `include/aegisproxy/<module>/`. Everything is in namespace
`aegis`, with sub-namespaces `net`, `http`, `proxy`, `lb`, `ds`, `health`, `log`.

## Conventions

* **Errors.** Fallible functions return `Result<T>` or `Status` (= `Result<void>`) from
  `common/result.hpp`. `ErrorCode::NotImplemented` marks scaffolded functionality.
  `Error::from_errno()` wraps system calls. No exceptions for control flow.
* **Ids, not pointers.** Cross-module references use `ConnectionId`, `BackendId`,
  `HandlerId` (`common/types.hpp`). Pointers returned by `find()` are valid only until the
  next mutating call or the end of the current event-loop round.
* **Thread-safety.** Nothing is thread-safe unless its header says so. The whole proxy
  runs on the event-loop thread. The logger level is atomic.
* **Queries are `[[nodiscard]]`**; getters are `noexcept`.

## common

| Type | Purpose |
|---|---|
| `Result<T>`, `Status`, `Error`, `ErrorCode` | uniform error handling |
| `Fd`, `ConnectionId`, `BackendId`, `HandlerId` | identifiers |
| `ProxyConfig`, `parse_config_text`, `load_config_file`, `validate_config` | configuration |
| `log::write/debug/info/warn/error`, `log::set_level` | logging |

## network (Member 1)

| Type | Key operations | Notes |
|---|---|---|
| `SocketHandle` | `get`, `valid`, `release`, `reset`; move-only | the only owner of an fd |
| `socket_ops::*` | `create_tcp_socket`, `listen_tcp`, `accept_tcp`, `connect_tcp`, `pending_error`, `local_port`, `is_would_block` | non-blocking + CLOEXEC always |
| `Buffer` | `prepare/commit`, `readable/consume`, `append`, `max_capacity` | bounded; `CapacityExceeded` |
| `EventMask`, `PollEvent`, `Poller` | `add/modify/remove/wait` | abstract; `EpollPoller` on Linux |
| `EventLoop` | `add(fd, mask, cb) → HandlerId`, `modify`, `remove`, `defer`, `run_once`, `run`, `stop` | dispatches by `HandlerId` |
| `ConnectionState`, `ConnectionStateMachine` | `transition_to`, `is_valid_transition` | rejects invalid transitions |
| `Connection` | `read_available`, `flush`, `inbound/outbound`, `state_machine`, `set_context` | owned by ConnectionManager |
| `ConnectionContext` | virtual dtor only | upper-layer per-connection state slot |
| `ConnectionManager` | `listen`, `adopt`, `find`, `request_write`, `close`, `set_data_handler`, `set_close_handler` | two-phase close |
| `BackpressureController` | `evaluate(queued, paused) → Decision` | TODO |

**Contract with upper layers:** the data handler receives `Connection&` whenever new
bytes are in `inbound()`. It must `consume()` what it parsed, may append to `outbound()`
and call `request_write(id)`, and may call `close(id)` — the `Connection&` stays valid
until the handler returns. It must not keep the reference.

## http (Member 2)

| Type | Key operations |
|---|---|
| `HttpMethod`, `HttpVersion`, `method_from_string`, `version_from_string`, `to_string` | enums + conversions (implemented) |
| `HttpHeader`, `iequals` | case-insensitive header names (implemented) |
| `HttpRequest` | `header(name)`, `content_length()`, `keep_alive()` (implemented) |
| `HttpResponse` | `serialize()`, `make_error_response(status, detail)`, `reason_phrase` (implemented) |
| `HttpParser` | `feed(string_view) → Progress{bytes_consumed, complete}`, `take_request`, `reset`, limits `kMax*` | **TODO** state machine |

## proxy (Member 2)

| Type | Key operations |
|---|---|
| `BackendConnector` | `begin_connect(endpoint) → Pending{socket, state}`, `finish_connect(fd)` (implemented) |
| `RequestForwarder` | `serialize_for_backend`, `forward(client, request, backend_fd)` — **TODO** |
| `HttpProxy` | `attach()`, `on_client_data(Connection&)` — currently answers 501 |

`HttpProxy` is constructed with references to `ConnectionManager`, `BackendRegistry`,
`LoadBalancingStrategy` and `BackendConnector`; it owns a `RequestForwarder`. Per-connection
proxy state (`ProxySession`, private to `http_proxy.cpp`) derives from
`net::ConnectionContext` and is stored on the `Connection`.

## datastructures (Member 3, header-only)

| Template | Operations | Status |
|---|---|---|
| `IndexedMinHeap<Key, Priority, Compare, KeyHash>` | `insert`, `remove`, `update_key`, `peek_min`, `extract_min`, `contains`, `size`, `check_invariants` | algorithms TODO |
| `HashTable<Key, Value, Hash, KeyEqual>` | `insert`, `find`, `erase`, `contains`, `rehash`, `load_factor`, `chain_length`, `check_invariants` | algorithms TODO |

## balancer (Member 3)

| Type | Key operations |
|---|---|
| `BackendEndpoint`, `HealthState`, `BackendStats`, `BackendEntry`, `CompletionInfo` | plain data |
| `BackendObserver` | `on_backend_added`, `on_health_changed`, `on_connection_started`, `on_connection_finished` |
| `BackendRegistry` | `add`, `find`, `entries`, `set_health`, `on_connection_started/finished`, `add/remove_observer` (implemented) |
| `LoadBalancingStrategy` (abstract, is a `BackendObserver`) | `select(SelectionContext) → BackendId`, `name()` |
| `RoundRobinBalancer`, `LeastConnectionsBalancer`, `AdaptiveBalancer` | `select` — **TODO** |
| `make_strategy(name, registry)` | factory (implemented) |

**How connection completion updates load statistics:** `RequestForwarder` (Member 2)
calls `registry.on_connection_started(id)` when a backend connection opens and
`registry.on_connection_finished(id, {latency, success})` when the exchange ends or
fails. The registry updates `BackendStats` and fans out to every observer, so
`LeastConnectionsBalancer` can `update_key` its heap and `AdaptiveBalancer` can update
its EWMA. No module other than the registry is called directly.

**How health changes propagate to strategies:** `HealthMonitor` (Member 3) calls
`registry.set_health(id, state)`; the registry notifies observers only on an actual
change via `on_health_changed(id, previous, current)`. A strategy adds the backend to its
candidate set on `→Healthy` and removes it on `→Unhealthy/Draining`. Strategies are
subscribed automatically by the `LoadBalancingStrategy` constructor and unsubscribed by its
destructor, so the registry must outlive them (it does: `main()` declares it first).

**Adding a strategy:** derive from `LoadBalancingStrategy`, implement `select` and the
observer hooks you need, add a case in `strategy_factory.cpp` and a name in
`validate_config`. Nothing in `network/` or `proxy/` changes.

## health (Member 3)

| Type | Key operations |
|---|---|
| `HealthConfig`, `ProbeOutcome` | plain data |
| `HealthMonitor` | `start`, `stop`, `tick(now) → due ids`, `record_probe_result(id, outcome)` — transitions TODO |

The monitor has no socket dependency; the probe I/O will be a small helper driven from
`main` once `EventLoop` has timers.
