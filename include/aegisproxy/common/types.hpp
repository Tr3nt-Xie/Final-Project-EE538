#pragma once

#include <cstdint>
#include <limits>

/// Fundamental identifier types shared by every module.
/// Keep this header dependency-free: everything includes it.
namespace aegis {

/// A raw POSIX file descriptor. Owned by SocketHandle; never close() a raw Fd directly.
using Fd = int;
inline constexpr Fd kInvalidFd = -1;

/// Identifies a client (or backend) Connection managed by ConnectionManager.
/// Ids are never reused within a process, which is the basis of stale-event detection
/// (see docs/ownership.md).
using ConnectionId = std::uint64_t;
inline constexpr ConnectionId kInvalidConnectionId = 0;

/// Identifies a registered backend in BackendRegistry (dense index, stable for the
/// registry's lifetime).
using BackendId = std::uint32_t;
inline constexpr BackendId kInvalidBackendId = std::numeric_limits<BackendId>::max();

/// Identifies a callback registration in EventLoop. Never reused within a process.
using HandlerId = std::uint64_t;
inline constexpr HandlerId kInvalidHandlerId = 0;

}  // namespace aegis
