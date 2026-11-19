# Development guide

## Prerequisites

| Tool | Version | Linux (Ubuntu 24.04) | macOS |
|---|---|---|---|
| CMake | ≥ 3.20 (presets need 3.25) | `apt install cmake` | `brew install cmake` |
| Compiler | GCC ≥ 13 or Clang ≥ 17 (C++20, `<format>`, `<span>`) | `apt install g++-13` / `clang-18` | Xcode CLT (Apple clang 17) |
| Ninja (optional) | any | `apt install ninja-build` | `brew install ninja` |
| GoogleTest | ≥ 1.11 | `apt install libgtest-dev` | `brew install googletest`, or let CMake fetch it |
| clang-format / clang-tidy | 18 | `apt install clang-format-18 clang-tidy-18` | `brew install llvm` |
| Docker (macOS/Windows) | any | — | Docker Desktop |

If GoogleTest is not installed, the build downloads v1.15.2 via `FetchContent` (needs
network). Set `-DAEGIS_FETCH_GTEST=OFF` to forbid that, or `-DAEGIS_BUILD_TESTS=OFF` to
skip tests entirely.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug       # or Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

With presets (recommended; build dirs under `build/<preset>`):

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan
cmake --workflow --preset ci       # configure + build + test with -Werror and sanitizers
```

Options: `AEGIS_BUILD_TESTS` (ON), `AEGIS_BUILD_BENCHMARKS` (OFF), `AEGIS_WARNINGS_AS_ERRORS`
(OFF locally, ON in CI), `AEGIS_ENABLE_ASAN`, `AEGIS_ENABLE_UBSAN`, `AEGIS_FETCH_GTEST`.

### macOS and other non-Linux hosts

The portable targets (`aegis_common`, `aegis_datastructures`, `aegis_http`,
`aegis_balancer`, `aegis_health`, `aegis_network` minus `EpollPoller`, and all unit
tests) build and run natively. `EventLoop::create()` returns `Unsupported`, the
integration tests are not built and `aegisproxy` cannot run. For the real thing:

```bash
scripts/docker-dev.sh            # Debug build + ctest in an Ubuntu 24.04 container
scripts/docker-dev.sh asan       # any configure preset name
scripts/docker-dev.sh shell      # interactive shell with the repo mounted at /work
```

## Run

```bash
./build/aegisproxy --config config/aegisproxy.example.conf --check-config   # parse only
./build/aegisproxy --config config/aegisproxy.example.conf [--log-level debug]
scripts/run-local.sh             # two Python backends + proxy, Ctrl-C stops all
curl -i http://127.0.0.1:8080/   # scaffold answers 501 until forwarding exists
```

`SIGINT`/`SIGTERM` stop the loop cleanly (exit code 0). Exit codes: 64 usage, 78 config
error, 2 startup failure, 3 event-loop failure.

## Tests

* Unit tests live in `tests/unit/<module>/`, one directory (and `CMakeLists.txt`) per
  module so that members never edit the same file. Each test binary links only the module
  under test. `tests/unit/network/fake_poller.hpp` drives `EventLoop` without epoll.
* Integration tests live in `tests/integration/{network,proxy,failover}/` (Members 1/2/3)
  and use a real epoll loop on `127.0.0.1:0`; `tests/integration/proxy_test_support.hpp`
  has the shared fixture and an in-process `StubBackend`.
* Every test carries a CTest label: `ctest -L member1|member2|member3|shared`.
* Tests for unimplemented behaviour are `DISABLED_`-prefixed and show up as "Disabled" in
  `ctest`; **remove the prefix in the PR that implements the feature.** Run a disabled test
  on purpose with `./build/tests/test_indexed_min_heap --gtest_also_run_disabled_tests`.
* Filter: `ctest --test-dir build -R Buffer`, or run the binary with `--gtest_filter=`.
* Always run the `asan` preset before opening a PR; CI runs it on GCC.

## Formatting and linting

```bash
scripts/format.sh            # clang-format -i on all sources
scripts/format.sh --check    # what CI runs
scripts/lint.sh build        # clang-tidy with the compile database from ./build
```

`.clang-format` is Google style with 100 columns; `.clang-tidy` enables bugprone,
cppcoreguidelines, modernize, performance, readability and misc with a few noisy checks
disabled. Fix or justify (`// NOLINT(check-name): reason`) every new warning.

## Debugging

* **Logs:** `--log-level debug` prints every accept/close with the open-connection count.
* **Sanitizers:** the `asan` preset catches use-after-free and most lifetime bugs; run
  the integration tests under it whenever you touch `ConnectionManager` or `EventLoop`.
* **gdb/lldb:** Debug builds have `-g`. For a hang, attach and look at the stack in
  `epoll_wait`; `EventLoop::handler_count()` and `stale_events_dropped()` are useful
  watch expressions.
* **strace:** `strace -f -e trace=network,epoll_ctl,epoll_wait ./build/aegisproxy …`
  shows exactly which fds are registered and what readiness arrives.
* **Load:** `wrk`, `ab` or `curl --parallel` against the proxy; see `benchmarks/README.md`.

## Adding a source file

Add it to the module's `src/<module>/CMakeLists.txt`; headers go under
`include/aegisproxy/<module>/`. Keep `network` free of includes from other modules
(`docs/architecture.md`, "Module boundaries").
