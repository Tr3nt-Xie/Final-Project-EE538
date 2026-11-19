// Integration test: a real EventLoop (epoll) + ConnectionManager + a raw client socket.
// Exercises accept → data → close end to end, plus the scaffold HttpProxy's 501 path.
#include "aegisproxy/balancer/backend_registry.hpp"
#include "aegisproxy/balancer/round_robin_balancer.hpp"
#include "aegisproxy/network/connection_manager.hpp"
#include "aegisproxy/network/event_loop.hpp"
#include "aegisproxy/proxy/backend_connector.hpp"
#include "aegisproxy/proxy/http_proxy.hpp"

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aegis {

using namespace std::chrono_literals;

namespace {

/// Blocks (outside the loop) until fd is readable or the timeout passes.
bool wait_readable(Fd fd, int timeout_ms) {
  pollfd p{fd, POLLIN, 0};
  return ::poll(&p, 1, timeout_ms) == 1;
}

/// Pumps the loop until `pred()` holds or `max_rounds` pass.
template <typename Pred>
bool pump_until(net::EventLoop& loop, Pred pred, int max_rounds = 50) {
  for (int i = 0; i < max_rounds && !pred(); ++i) {
    auto r = loop.run_once(20ms);
    if (!r)
      return false;
  }
  return pred();
}

struct Fixture {
  std::unique_ptr<net::EventLoop> loop;
  std::unique_ptr<net::ConnectionManager> mgr;
  std::uint16_t port{0};

  Fixture(net::ConnectionLimits limits = {}) {
    auto l = net::EventLoop::create();
    EXPECT_TRUE(l) << l.error().to_string();
    loop = std::move(*l);
    mgr = std::make_unique<net::ConnectionManager>(*loop, limits);
    EXPECT_TRUE(mgr->listen("127.0.0.1", 0, 16));
    port = *mgr->listen_port();
  }

  net::SocketHandle connect_client() {
    auto c = net::socket_ops::connect_tcp("127.0.0.1", port);
    EXPECT_TRUE(c) << c.error().to_string();
    return std::move(c->first);
  }
};

}  // namespace

TEST(ConnectionLifecycle, AcceptDeliverDataAndCloseOnPeerEof) {
  Fixture f;
  std::string received;
  ConnectionId seen_id = kInvalidConnectionId;
  int closes = 0;
  f.mgr->set_data_handler([&](net::Connection& c) {
    seen_id = c.id();
    received += c.inbound().view();
    c.inbound().consume(c.inbound().readable_bytes());
  });
  f.mgr->set_close_handler([&](ConnectionId) { ++closes; });

  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  EXPECT_EQ(f.mgr->total_accepted(), 1U);

  ASSERT_EQ(::send(client.get(), "ping", 4, 0), 4);
  ASSERT_TRUE(pump_until(*f.loop, [&] { return received == "ping"; }));
  ASSERT_NE(seen_id, kInvalidConnectionId);
  net::Connection* conn = f.mgr->find(seen_id);
  ASSERT_NE(conn, nullptr);
  EXPECT_EQ(conn->state(), net::ConnectionState::ReadingRequest);

  client.reset();  // peer EOF → manager must close and destroy the connection
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 0; }));
  EXPECT_EQ(closes, 1);
  EXPECT_EQ(f.mgr->find(seen_id), nullptr);
}

TEST(ConnectionLifecycle, CloseFromInsideDataHandlerIsSafe) {
  Fixture f;
  f.mgr->set_data_handler([&](net::Connection& c) {
    ASSERT_TRUE(f.mgr->close(c.id()));
    // Still a valid object here: destruction is deferred until after this callback.
    EXPECT_EQ(c.state(), net::ConnectionState::Closed);
  });
  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  ASSERT_EQ(::send(client.get(), "x", 1, 0), 1);
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 0; }));
  // Server closed: the client observes EOF.
  ASSERT_TRUE(wait_readable(client.get(), 1000));
  char buf[8];
  EXPECT_EQ(::recv(client.get(), buf, sizeof buf, 0), 0);
}

TEST(ConnectionLifecycle, ConnectionLimitIsEnforced) {
  Fixture f(net::ConnectionLimits{.max_connections = 1, .buffer_limit_bytes = 4096});
  net::SocketHandle a = f.connect_client();
  net::SocketHandle b = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->total_accepted() >= 1; }));
  (void)f.loop->run_once(50ms);
  EXPECT_EQ(f.mgr->size(), 1U) << "second connection must be dropped, not adopted";
}

TEST(ConnectionLifecycle, WriteInterestIsEnabledAndDrained) {
  Fixture f;
  f.mgr->set_data_handler([&](net::Connection& c) {
    ASSERT_TRUE(c.outbound().append("pong"));
    c.inbound().clear();
    ASSERT_TRUE(f.mgr->request_write(c.id()));
  });
  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  ASSERT_EQ(::send(client.get(), "ping", 4, 0), 4);
  (void)pump_until(*f.loop, [] { return false; }, 5);
  ASSERT_TRUE(wait_readable(client.get(), 1000));
  char buf[16] = {};
  const ssize_t n = ::recv(client.get(), buf, sizeof buf, 0);
  EXPECT_EQ(std::string(buf, static_cast<std::size_t>(n > 0 ? n : 0)), "pong");
  EXPECT_EQ(f.mgr->size(), 1U) << "connection stays open after a reply";
}

TEST(HttpProxyScaffold, AnswersEveryRequestWith501AndCloses) {
  Fixture f;
  lb::BackendRegistry registry;
  registry.add(lb::BackendEndpoint{"b", "127.0.0.1", 1, 1});
  lb::RoundRobinBalancer strategy(registry);
  proxy::BackendConnector connector;
  proxy::HttpProxy http_proxy(*f.mgr, registry, strategy, connector);
  http_proxy.attach();

  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  const std::string req = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
  ASSERT_EQ(::send(client.get(), req.data(), req.size(), 0), static_cast<ssize_t>(req.size()));
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 0; }));
  EXPECT_EQ(http_proxy.requests_seen(), 1U);

  std::string response;
  char buf[512];
  while (wait_readable(client.get(), 500)) {
    const ssize_t n = ::recv(client.get(), buf, sizeof buf, 0);
    if (n <= 0)
      break;
    response.append(buf, static_cast<std::size_t>(n));
  }
  EXPECT_EQ(response.rfind("HTTP/1.1 501 Not Implemented\r\n", 0), 0U) << response;
  EXPECT_NE(response.find("Connection: close"), std::string::npos);
  EXPECT_NE(response.find("not implemented"), std::string::npos);
}

}  // namespace aegis
