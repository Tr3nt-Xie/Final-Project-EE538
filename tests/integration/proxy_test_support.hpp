#pragma once
// Helpers shared by the integration tests (all members). Keep this header small and
// dependency-free so that changing it does not force merges across workstreams.

#include "aegisproxy/network/connection_manager.hpp"
#include "aegisproxy/network/event_loop.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aegis::testing {

/// Blocks (outside the loop) until fd is readable or the timeout passes.
inline bool wait_readable(Fd fd, int timeout_ms) {
  pollfd p{fd, POLLIN, 0};
  return ::poll(&p, 1, timeout_ms) == 1;
}

/// Reads until the peer closes or the per-read timeout passes.
inline std::string read_until_close(Fd fd, int timeout_ms = 500) {
  std::string out;
  char buf[1024];
  while (wait_readable(fd, timeout_ms)) {
    const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
    if (n <= 0)
      break;
    out.append(buf, static_cast<std::size_t>(n));
  }
  return out;
}

/// Pumps the loop until `pred()` holds or `max_rounds` pass.
template <typename Pred>
bool pump_until(net::EventLoop& loop, Pred pred, int max_rounds = 50) {
  for (int i = 0; i < max_rounds && !pred(); ++i) {
    auto r = loop.run_once(std::chrono::milliseconds{20});
    if (!r)
      return false;
  }
  return pred();
}

/// EventLoop + ConnectionManager listening on an ephemeral 127.0.0.1 port.
struct ProxyFixture {
  std::unique_ptr<net::EventLoop> loop;
  std::unique_ptr<net::ConnectionManager> mgr;
  std::uint16_t port{0};

  explicit ProxyFixture(net::ConnectionLimits limits = {}) {
    auto l = net::EventLoop::create();
    if (l) {
      loop = std::move(*l);
      mgr = std::make_unique<net::ConnectionManager>(*loop, limits);
      if (mgr->listen("127.0.0.1", 0, 16))
        port = mgr->listen_port().value_or(0);
    }
  }
  [[nodiscard]] bool ok() const { return loop && mgr && port != 0; }

  net::SocketHandle connect_client() {
    auto c = net::socket_ops::connect_tcp("127.0.0.1", port);
    return c ? std::move(c->first) : net::SocketHandle{};
  }
};

/// Minimal blocking HTTP backend running on its own thread: accepts connections and
/// answers every request with `response` (a complete HTTP/1.1 response), then closes.
/// Good enough to prove forwarding and failover end to end without Python.
class StubBackend {
 public:
  explicit StubBackend(std::string response) : response_(std::move(response)) {
    auto l = net::socket_ops::listen_tcp("127.0.0.1", 0, 16);
    if (!l)
      return;
    listener_ = std::move(*l);
    port_ = net::socket_ops::local_port(listener_.get()).value_or(0);
    thread_ = std::thread([this] { serve(); });
  }
  ~StubBackend() { stop(); }
  StubBackend(const StubBackend&) = delete;
  StubBackend& operator=(const StubBackend&) = delete;

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  [[nodiscard]] int requests_served() const noexcept { return served_.load(); }

  /// Stops accepting (simulates a crashed backend). Idempotent.
  void stop() {
    stopping_ = true;
    if (thread_.joinable())
      thread_.join();
    listener_.reset();
  }

 private:
  void serve() {
    while (!stopping_) {
      if (!wait_readable(listener_.get(), 20))
        continue;
      auto accepted = net::socket_ops::accept_tcp(listener_.get());
      if (!accepted || !accepted->has_value())
        continue;
      net::SocketHandle client = std::move(**accepted);
      if (wait_readable(client.get(), 1000)) {
        char buf[4096];
        (void)::recv(client.get(), buf, sizeof buf, 0);  // consume the request
      }
      (void)::send(client.get(), response_.data(), response_.size(), 0);
      ++served_;
    }
  }

  std::string response_;
  net::SocketHandle listener_;
  std::uint16_t port_{0};
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  std::atomic<int> served_{0};
};

}  // namespace aegis::testing
