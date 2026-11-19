# Architecture

## Goal and constraints

AegisProxy is a single-threaded, event-driven HTTP/1.1 reverse proxy. The framework
fixes the following decisions so that three developers can work independently:

* **One epoll event loop, level-triggered, non-blocking sockets.** No thread pool, no
  thread-per-connection. Level-triggered means an fd is reported until drained, which
  tolerates handlers that do not read everything in one go. Edge-triggered is a later
  optimisation (Member 1).
* **No networking frameworks.** Only POSIX sockets, epoll and the standard library;
  GoogleTest is the one third-party dependency (tests only).
* **No global mutable state** except the logger's level. Every component is constructed
  in `main()` and passed by reference downward.
* **Interfaces before implementations.** Abstract classes (`Poller`,
  `LoadBalancingStrategy`, `BackendObserver`, `ConnectionContext`) decouple modules;
  implementations live in `.cpp` files or clearly marked skeletons.

## Pipeline

```
          ┌────────────────────────────── network (Member 1) ───────────────────────────┐
client ──▶│ listener fd ─▶ EventLoop ─▶ ConnectionManager ─▶ Connection{inbound Buffer} │
          └───────────────────────────────────────┬──────────────────────────────────────┘
                                                  │ data handler (std::function)
          ┌────────────────── http + proxy (Member 2) ─────────────────────────────────┐
          │ HttpProxy ─▶ HttpParser ─▶ HttpRequest ─▶ RequestForwarder ─▶ BackendConnector │
          └──────────┬────────────────────────────────────────────────────┬──────────────┘
                     │ select()                                           │ backend socket
          ┌──────────▼──── balancer + health (Member 3) ────────┐         ▼
          │ LoadBalancingStrategy ◀─ BackendRegistry ◀─ HealthMonitor │   backend server
          └───────────────────────────────────────────────────┘
```

Responses travel the same path in reverse: backend socket → `RequestForwarder` →
`Connection::outbound()` → `ConnectionManager::request_write()` → `EventLoop` Writable →
`Connection::flush()`.

## Module boundaries and dependency direction

```
common  ◀── datastructures ◀── balancer ◀── health
  ▲                               ▲
  │                               │
network ◀── proxy ──▶ http ───────┘
  ▲          ▲
  └── main ──┘
```

* `common` depends on nothing. `datastructures` is header-only and depends on `common`.
* `network` depends only on `common`. It never includes `http`, `proxy`, `balancer` or
  `health`. Upper layers reach it through the `ConnectionManager` handlers (data /
  writable / close) and the polymorphic `ConnectionContext` slot on `Connection`; they
  never call `EventLoop` for sockets.
* `http` depends only on `common` (pure protocol code, no sockets).
* `balancer` depends on `common` + `datastructures`; `health` depends on `balancer`.
  Neither touches sockets: all load/health information flows through `BackendRegistry`.
* `proxy` is the integration layer: it depends on `network`, `http` and `balancer`.
* `main` wires everything and owns all top-level objects.

There are no cycles. CMake enforces the direction through target link dependencies;
adding a forbidden include fails to compile because the include directory is shared,
so reviewers check this (CONTRIBUTING.md checklist).

## Request lifecycle (target behaviour)

1. **Accept.** Listener fd becomes Readable → `ConnectionManager::on_listener_ready`
   drains `accept()` until `EAGAIN`, wrapping each fd in a `SocketHandle` →
   `adopt()` creates a `Connection`, registers it with `EventLoop` for Readable with a
   callback bound to the **ConnectionId**, and moves the state machine to
   `ReadingRequest`.
2. **Read.** Client fd Readable → `Connection::read_available()` fills `inbound()` →
   data handler → `HttpProxy::on_client_data` → `HttpParser::feed(inbound().view())` →
   `inbound().consume(bytes_consumed)`. Partial requests simply wait for more events.
3. **Select.** When a request is complete: `SelectingBackend`;
   `LoadBalancingStrategy::select()` returns a `BackendId` or `NoHealthyBackend` (→ 503).
4. **Connect.** `ConnectingBackend`; `BackendConnector::begin_connect()` returns a
   non-blocking socket (`InProgress`); the proxy hands it to
   `ConnectionManager::adopt_backend(socket, client_id)`, which supervises the connect and
   reports completion through the writable handler (or failure through the close handler
   with `last_error()`). Failure → failover to another backend, or 502 after N attempts.
   `BackendRegistry::on_connection_started`.
5. **Forward.** `ForwardingRequest`; `RequestForwarder` serialises the request
   (hop-by-hop headers stripped, `X-Forwarded-For` added) into a backend-side buffer and
   streams the request body.
6. **Respond.** `AwaitingResponse` → bytes from the backend are appended to
   `client.outbound()`; `request_write()` enables Writable; `WritingResponse` until drained.
   `BackendRegistry::on_connection_finished(latency, success)` updates load statistics and
   notifies strategies.
7. **Keep-alive or close.** HTTP/1.1 without `Connection: close` → back to
   `ReadingRequest` with `parser.reset()`; otherwise `Closing` → `ConnectionManager::close`.

Today, step 2 ends with a `501 Not Implemented` response (the parser is a stub) so the
system fails loudly end to end; the integration test asserts exactly that.

## Backpressure (target behaviour)

Every `Buffer` has a `max_capacity`. `BackpressureController::evaluate(queued, paused)`
returns `PauseReads` when a peer's outbound queue crosses the high watermark and
`ResumeReads` once it drains below the low watermark; the ProxySession translates that
into `EventLoop::modify()` calls that drop/restore Readable interest on the *other* socket.
Until it is implemented, a full inbound buffer closes the connection (logged as a warning).

## Health and failover (target behaviour)

`HealthMonitor` is a pure state machine over probe outcomes; the probing I/O is driven
from the event loop (`tick(now)` → probe → `record_probe_result`). It calls
`BackendRegistry::set_health`, which fans out `on_health_changed` to every
`LoadBalancingStrategy`, which drops/re-adds the backend in its candidate set. Failover is
therefore two mechanisms: passive (strategies stop choosing Unhealthy backends) and
active (`RequestForwarder` retries another backend when a connect fails).

## What the framework deliberately does not decide yet

* Timer facility on `EventLoop` (needed by health probes, connect timeouts, idle
  timeouts). Proposed: `timerfd` registered like any other fd. — Member 1, week 2.
* Thread model beyond one loop. Possible later: N loops with `SO_REUSEPORT`; strategies
  would then need per-loop state or a lock. Out of scope for the course deliverable.
* Hostname resolution (`socket_ops::make_ipv4` accepts IPv4 literals only).
