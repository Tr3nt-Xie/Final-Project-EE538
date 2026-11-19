# Ownership and lifetimes

The rule of thumb: **every resource has exactly one owner, and nobody stores a pointer to
something they do not own across an event-loop round.** References are held only for the
duration of a call; cross-round references are ids.

## Object graph at runtime

```
main()
 ├─ ProxyConfig                      (value)
 ├─ unique_ptr<EventLoop>            owns unique_ptr<Poller> (EpollPoller) + wake pipe
 ├─ BackendRegistry                  owns vector<BackendEntry>; holds observer pointers
 ├─ unique_ptr<LoadBalancingStrategy> refs BackendRegistry (subscribes/unsubscribes itself)
 ├─ HealthMonitor                    refs BackendRegistry
 ├─ ConnectionManager                refs EventLoop; owns listener SocketHandle and
 │                                    map<ConnectionId, unique_ptr<Connection>>
 │     └─ Connection                 owns client SocketHandle, inbound/outbound Buffer,
 │                                    ConnectionStateMachine, unique_ptr<ConnectionContext>
 │           └─ ProxySession (ConnectionContext)   owns HttpParser; will own the backend
 │                                                  SocketHandle + its HandlerId
 ├─ BackendConnector                 stateless
 ├─ HttpProxy                        refs ConnectionManager, BackendRegistry, Strategy, Connector
 └─ SignalStopper                    owns signalfd SocketHandle; refs EventLoop
```

Declaration order in `main()` is destruction order reversed, so every reference holder is
destroyed before its target.

## Resource-by-resource rules

| Resource | Owner | Lifetime | Notes |
|---|---|---|---|
| **Client socket (fd)** | `Connection::socket_` (`SocketHandle`) | adopt() → deferred destruction after close() | Never exposed as an owning raw fd. `fd()` is for registering with the loop only. |
| **Backend socket (fd)** | `ProxySession` (planned) | begin_connect() → end of the exchange or error | Must be `EventLoop::remove`d before the `SocketHandle` is reset. Pooling is a later design change. |
| **Listener socket** | `ConnectionManager::listener_` | listen() → ~ConnectionManager | |
| **epoll fd, wake pipe, signalfd** | `EpollPoller`, `EventLoop`, `SignalStopper` | component lifetime | all via `SocketHandle` (it is just an fd owner) |
| **Connection objects** | `ConnectionManager::connections_` (`unique_ptr`) | see "Two-phase close" | Non-copyable, non-movable: the loop callback captures the id, never `this`. |
| **Buffers** | `Connection` (`inbound_`, `outbound_`) | same as the Connection | Upper layers get `Buffer&` during a callback; `view()`/`readable()` spans are invalidated by `prepare/append/consume`. |
| **Event callbacks** | `EventLoop::handlers_` (`shared_ptr<Entry>`) | add() → remove() | The loop holds a local `shared_ptr` while invoking, so a callback that removes itself does not destroy its own `std::function`. Callbacks capture `this` of long-lived owners (`ConnectionManager`, `SignalStopper`) plus an id — never a `Connection*`. |
| **Backend registry entries** | `BackendRegistry::entries_` | add() → ~BackendRegistry | No removal API by design: ids are dense indices and must stay stable. Take a backend out of rotation with `HealthState::Draining`. |
| **Observers** | not owned (raw pointers in the registry) | must `remove_observer` before dying | `LoadBalancingStrategy` does this in its destructor. |
| **ConnectionContext / ProxySession** | `Connection::context_` | set_context() → Connection destruction | Lets `proxy` attach state to a `Connection` without `network` knowing `proxy` exists. |

## Two-phase close (preventing use-after-free)

`ConnectionManager::close(id)`:

1. moves the `unique_ptr<Connection>` out of the map (so `find(id)` returns `nullptr`
   from now on);
2. calls `EventLoop::remove(handler)` — the fd is deleted from epoll **before** the
   `SocketHandle` closes it;
3. transitions the state machine to `Closing` → `Closed` and runs the close handler;
4. **defers** the actual destruction via `EventLoop::defer()`.

Because destruction is deferred until after the current dispatch round, `close()` is
safe to call from inside the connection's own data handler or event callback: the
`Connection&` the caller is holding stays valid until that callback returns. The
integration test `ConnectionLifecycle.CloseFromInsideDataHandlerIsSafe` covers this.

## Stale events and file-descriptor reuse

Two things can go wrong with epoll and closing sockets:

1. **Stale event in the same round.** `epoll_wait` returned events for fds A and B; the
   handler for A closes B. The event for B is still in the batch.
2. **Fd number reuse.** B's fd is closed, `accept()` returns the same number for a new
   client C, and a leftover event meant for B arrives.

The framework addresses both with one mechanism: **epoll's user data is a `HandlerId`,
never the fd.** `HandlerId`s are allocated from a monotonically increasing counter and
never reused. On every event, `EventLoop::run_once` looks the id up in `handlers_`:

* If B's handler was removed earlier in the round, the lookup fails and the event is
  dropped (`stale_events_dropped()` counts it; the fake-poller unit test
  `StaleEventsForRemovedHandlerAreDropped` covers this).
* If the fd was reused by C, C has a *different* `HandlerId`, so an old event for B's id
  cannot be routed to C. (In practice the kernel also removes a closed fd from the
  interest list, but we do not rely on that.)

The same idea is applied one level up: `ConnectionManager`'s loop callbacks capture a
`ConnectionId` and look the connection up per event; `ConnectionId`s are never reused.

Rules that keep this sound:

* Always `EventLoop::remove()` **before** the `SocketHandle` closes (the destructor of
  `ConnectionManager` and `close()` both do this). Removing after close is harmless in
  epoll (`EBADF` is ignored) but ordering it correctly avoids surprises with fd reuse
  between the two calls.
* Never cache a `Connection*` across rounds; re-`find(id)` it.
* Never call `close()` on an fd directly; `SocketHandle::reset()` is the only path.

## Backend sockets (guidance for Member 2)

A backend socket is owned by the `ProxySession` of the client connection it serves. Its
event callback captures the **client** `ConnectionId` and looks the session up via
`ConnectionManager::find(id)->context()`. When the client connection closes, the session
(and thus the backend `SocketHandle`) is destroyed with it — but the session destructor
must first `EventLoop::remove()` the backend handler, so the session needs a reference to
the loop (or to `ConnectionManager`, which can expose a helper). If a backend event arrives
after the client closed, the `find(id)` lookup fails and the event is dropped, exactly like
the client case.

## Thread-safety summary

Single-threaded. `EventLoop::stop()` may be called from a loop callback (it is invoked
via `signalfd` on the loop thread in `main.cpp`); calling it from another thread would
require making `stop_requested_` atomic — not needed today. The logger level is the only
atomic.
