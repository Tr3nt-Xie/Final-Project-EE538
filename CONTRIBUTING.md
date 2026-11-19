# Contributing

## Branches

| Branch | Owner | Scope |
|---|---|---|
| `main` | Technical Lead merges | always compiles, always green CI |
| `feature/network-core` | Member 1 | `network/`, build system, CI |
| `feature/http-proxy` | Member 2 | `http/`, `proxy/` |
| `feature/load-balancer` | Member 3 | `balancer/`, `datastructures/`, `health/` |

Short-lived topic branches off your feature branch are fine (`feature/http-proxy/chunked`).
Rebase your feature branch on `main` at least at every integration checkpoint
(`docs/milestones.md`); never force-push `main`.

## Commit messages

Conventional-commit style, imperative mood, ≤ 72-char subject, module scope in parentheses:

```
feat(http): parse request line and headers
fix(network): drop Writable interest once outbound buffer drains
test(balancer): enable round-robin selection tests
docs(ownership): describe backend socket lifetime
refactor(datastructures): extract sift helpers
build: raise minimum CMake to 3.25 for presets v6
```

Body (optional): what and why, not how. Reference the milestone or issue (`Refs M2`).

## Pull requests

* One logical change per PR; keep diffs reviewable (< ~500 lines preferred).
* Fill in `.github/PULL_REQUEST_TEMPLATE.md`. CI must be green.
* Cross-module changes (anything under `common/`, a header another member includes,
  `CMakeLists.txt`) need the Technical Lead as reviewer; everything else needs one
  other member.
* Re-enable `DISABLED_` tests in the same PR that implements the behaviour.

## Code review checklist

**Correctness**
- [ ] Every failure path returns a `Result`/`Status` error; no silent success, no `assert` as error handling
- [ ] Unimplemented paths return `ErrorCode::NotImplemented`
- [ ] No raw `new`/`delete`, no raw fd stored outside a `SocketHandle`
- [ ] No pointer to a `Connection`, `Buffer` or registry entry held across an event-loop round
- [ ] `EventLoop::remove` is called before the owning `SocketHandle` closes
- [ ] Non-blocking I/O loops handle `EAGAIN`, `EINTR`, partial reads/writes and EOF

**Design**
- [ ] No new include from `network/` into `http/`, `proxy/`, `balancer/` or `health/` (dependency direction is the other way; see `docs/interfaces.md`)
- [ ] Ownership/lifetime rules in `docs/ownership.md` still hold (update the doc if not)
- [ ] Strongly typed enums, `const`-correct, `[[nodiscard]]` on queries and `Result`s
- [ ] No hot-path allocation introduced without a comment justifying it

**Tests & tooling**
- [ ] New behaviour has a unit test; integration test added/updated when the wire behaviour changes
- [ ] Tests pass under the `asan` preset
- [ ] `scripts/format.sh --check` passes; `scripts/lint.sh` has no new warnings

## Integration guidelines

1. Interfaces first: change a public header only after the affected owner has agreed
   (a short comment on the PR is enough). Prefer adding over changing.
2. Keep `main` runnable: every merged PR must leave `./aegisproxy --config … --check-config`
   working and `ctest` green.
3. Stubs over branches: if your feature needs something another member has not finished,
   code against the interface and leave a `TODO(Member N)`; do not fork their module.
4. Integration checkpoints (end of weeks 2, 4, 6 — see `docs/milestones.md`): everyone
   rebases on `main`, the TL runs the end-to-end script, and the group fixes integration
   bugs together before new feature work resumes.
