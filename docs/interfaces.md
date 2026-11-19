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
| `Connection` | `role`, `peer_id`, `read_available`, `flush`, `inbound/outbound`, `state_machine`, `last_error`, `bytes_read/written`, `set_context` | owned by ConnectionManager |
| `ConnectionContext` | virtual dtor only | upper-layer per-connection state slot |
| `ConnectionManager` | `listen`, `adopt`, `adopt_backend`*, `find`, `request_write`, `pause_reads`*, `resume_reads`*, `close`, `stats`, `set_data_handler`, `set_writable_handler`, `set_close_handler` | two-phase close; * = TODO(Member 1) |
| `NetworkStats` | `open_connections`, `total_accepted/rejected/closed`, `total_backend_connects`, `bytes_in/out` | snapshot for Member 3 / benchmarks |
| `BackpressureController` | `evaluate(queued, paused) → Decision` | TODO |

**Contract with upper layers:** three handlers, each receiving `Connection&` valid only
until the handler returns (never keep the reference; re-`find(id)` next time):

* *data handler* — new bytes are in `inbound()`; `consume()` what you parsed, append to
  `outbound()` and call `request_write(id)` if you have something to send.
* *writable handler* — `outbound()` has drained (write more, or close), **or** a backend
  connection just finished connecting (state moved `ConnectingBackend → ForwardingRequest`).
* *close handler* — the connection is about to be destroyed; `last_error()` tells a
  socket error / refused connect apart from a clean EOF; `role()`/`peer_id()` tell you
  which side died so you can close or fail over the other.

Any handler may call `close(id)` on any connection, including the one it was called for.

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
| `RequestForwarder` | `serialize_for_backend`, `forward(connections, client_id, request, backend_id)` — **TODO** |
| `HttpProxy` | `attach()`, `on_client_data(Connection&)` — currently answers 501 |

`HttpProxy` is constructed with references to `ConnectionManager`, `BackendRegistry`,
`LoadBalancingStrategy` and `BackendConnector`; it owns a `RequestForwarder`. Per-connection
proxy state (`ProxySession`, private to `http_proxy.cpp`) derives from
`net::ConnectionContext` and is stored on the client `Connection`. Backend sockets are
handed to `ConnectionManager::adopt_backend` and addressed by `ConnectionId` from then on;
`proxy/` never includes `event_loop.hpp` for anything but the type name.

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

## Cross-member contracts

### Member 1 ↔ Member 2 (sockets ↔ HTTP)

| Direction | What | How | Errors | Ownership |
|---|---|---|---|---|
| 1 → 2 | readiness events | the three `ConnectionManager` handlers above | socket errors arrive as `close` with `last_error()` set | `Connection`, its `Buffer`s and fds belong to `ConnectionManager`; Member 2 holds only `ConnectionId`s |
| 1 → 2 | connection handles | `ConnectionId`, `find(id)` for the duration of a call | `find` returns `nullptr` after close — always check | — |
| 1 → 2 | buffer operations | `Buffer::view/readable/consume/append/prepare/commit`, `request_write`, `pause_reads`, `resume_reads` | `CapacityExceeded` from `append/prepare`; `NotFound` for a dead id | — |
| 2 → 1 | backend socket to supervise | `BackendConnector::begin_connect` → `adopt_backend(socket, client_id)` | `InvalidArgument` (bad socket), `NotFound` (client gone), `SystemError` on connect failure via the close handler | the `SocketHandle` moves into the manager |
| rule | **Member 2 never calls `EventLoop::add/modify/remove` or touches epoll.** | | | |

### Member 1 ↔ Member 3 (lifecycle ↔ scheduling)

| Direction | What | How | Errors | Ownership |
|---|---|---|---|---|
| 1 → 3 | connection lifecycle notifications | `ConnectionManager` close handler (role, peer, `last_error`, byte counts) — consumed by Member 2's proxy, which translates them into `BackendRegistry::on_connection_finished` | — | — |
| 1 → 3 | networking statistics | `ConnectionManager::stats()` (`NetworkStats`), `Connection::bytes_read/written` | — | snapshot by value |
| 3 → 1 | nothing direct | strategies and health never see sockets | — | — |
| rule | **Member 3 never owns client (or backend) socket descriptors.** Backend *endpoints* (host/port) live in the registry; connecting is Member 2's job via Member 1's API. | | | |

### Member 2 ↔ Member 3 (forwarding ↔ balancing)

| Direction | What | How | Errors | Ownership |
|---|---|---|---|---|
| 2 → 3 | "pick a backend" | `LoadBalancingStrategy::select(SelectionContext) → Result<BackendId>` | `NoHealthyBackend` → Member 2 answers 503; `NotImplemented` while stubbed | strategy is owned by `main`, passed by reference |
| 3 → 2 | endpoint for that id | `BackendRegistry::find(id)->endpoint` (`host`, `port`, `name`, `weight`) | `find` returns `nullptr` only for an invalid id | registry entries are immutable except `health`/`stats` |
| 2 → 3 | request started | `BackendRegistry::on_connection_started(id)` right after `adopt_backend` succeeds | `NotFound` | — |
| 2 → 3 | request finished / failed + latency | `BackendRegistry::on_connection_finished(id, CompletionInfo{latency, success})` exactly once per `on_connection_started` (also on connect failure with `success=false`) | `InvalidState` if unbalanced — treat as a bug | — |
| 3 → 2 | health changes | none directly: strategies react internally; Member 2 just calls `select()` again | — | — |
| joint | failover policy | Member 2 retries `select()` + connect up to *N* times (skipping the failed id via `SelectionContext`, to be added), then 502; Member 3's health monitor marks the backend Unhealthy after `unhealthy_threshold` probe failures | agree *N* and the skip mechanism at Checkpoint A | — |
