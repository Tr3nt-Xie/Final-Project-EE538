// Integration test (Member 1): a real EventLoop (epoll) + ConnectionManager + raw client
// sockets. Exercises accept → data → close end to end.
#include "aegisproxy/network/connection_manager.hpp"

#include <string>

#include <gtest/gtest.h>

#include <sys/socket.h>

#include "../proxy_test_support.hpp"

namespace aegis {

using testing::ProxyFixture;
using testing::pump_until;
using testing::wait_readable;

TEST(ConnectionLifecycle, AcceptDeliverDataAndCloseOnPeerEof) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
  std::string received;
  ConnectionId seen_id = kInvalidConnectionId;
  int closes = 0;
  f.mgr->set_data_handler([&](net::Connection& c) {
    seen_id = c.id();
    received += c.inbound().view();
    c.inbound().consume(c.inbound().readable_bytes());
  });
  f.mgr->set_close_handler([&](net::Connection& c) {
    ++closes;
    EXPECT_EQ(c.role(), net::ConnectionRole::Client);
    EXPECT_EQ(c.state(), net::ConnectionState::Closed);
    EXPECT_FALSE(c.last_error().has_value()) << "clean EOF is not an error";
    EXPECT_EQ(c.bytes_read(), 4U);
  });

  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  EXPECT_EQ(f.mgr->total_accepted(), 1U);

  ASSERT_EQ(::send(client.get(), "ping", 4, 0), 4);
  ASSERT_TRUE(pump_until(*f.loop, [&] { return received == "ping"; }));
  ASSERT_NE(seen_id, kInvalidConnectionId);
  net::Connection* conn = f.mgr->find(seen_id);
  ASSERT_NE(conn, nullptr);
  EXPECT_EQ(conn->state(), net::ConnectionState::ReadingRequest);
  EXPECT_EQ(conn->role(), net::ConnectionRole::Client);
  EXPECT_EQ(conn->peer_id(), kInvalidConnectionId);

  client.reset();  // peer EOF → manager must close and destroy the connection
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 0; }));
  EXPECT_EQ(closes, 1);
  EXPECT_EQ(f.mgr->find(seen_id), nullptr);
  const net::NetworkStats s = f.mgr->stats();
  EXPECT_EQ(s.open_connections, 0U);
  EXPECT_EQ(s.total_accepted, 1U);
  EXPECT_EQ(s.total_closed, 1U);
}

TEST(ConnectionLifecycle, CloseFromInsideDataHandlerIsSafe) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
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
  ProxyFixture f(net::ConnectionLimits{.max_connections = 1, .buffer_limit_bytes = 4096});
  ASSERT_TRUE(f.ok());
  net::SocketHandle a = f.connect_client();
  net::SocketHandle b = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->total_accepted() >= 1; }));
  (void)f.loop->run_once(std::chrono::milliseconds{50});
  EXPECT_EQ(f.mgr->size(), 1U) << "second connection must be dropped, not adopted";
  EXPECT_EQ(f.mgr->stats().total_rejected, 1U);
}

TEST(ConnectionLifecycle, WriteInterestIsEnabledDrainedAndReported) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
  int drained = 0;
  f.mgr->set_data_handler([&](net::Connection& c) {
    ASSERT_TRUE(c.outbound().append("pong"));
    c.inbound().clear();
    ASSERT_TRUE(f.mgr->request_write(c.id()));
  });
  f.mgr->set_writable_handler([&](net::Connection& c) {
    ++drained;
    EXPECT_FALSE(c.has_pending_output());
    EXPECT_FALSE(has(c.interest(), net::EventMask::Writable)) << "Writable dropped once drained";
  });
  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  ASSERT_EQ(::send(client.get(), "ping", 4, 0), 4);
  ASSERT_TRUE(pump_until(*f.loop, [&] { return drained == 1; }));
  ASSERT_TRUE(wait_readable(client.get(), 1000));
  char buf[16] = {};
  const ssize_t n = ::recv(client.get(), buf, sizeof buf, 0);
  EXPECT_EQ(std::string(buf, static_cast<std::size_t>(n > 0 ? n : 0)), "pong");
  EXPECT_EQ(f.mgr->size(), 1U) << "connection stays open after a reply";
}

TEST(ConnectionLifecycle, BackendAdoptionAndReadPausingAreExplicitlyUnimplemented) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  const ConnectionId cid = f.mgr->find(1) ? 1 : kInvalidConnectionId;
  ASSERT_NE(cid, kInvalidConnectionId);

  auto sock = net::socket_ops::create_tcp_socket();
  ASSERT_TRUE(sock);
  EXPECT_EQ(f.mgr->adopt_backend(std::move(*sock), cid).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(f.mgr->adopt_backend(net::SocketHandle{}, cid).error().code,
            ErrorCode::InvalidArgument);
  EXPECT_EQ(f.mgr->pause_reads(cid).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(f.mgr->resume_reads(cid).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(f.mgr->pause_reads(999).error().code, ErrorCode::NotFound);
}

// ---- Pending (Member 1): enable when adopt_backend / pause_reads / resume_reads land ----

TEST(ConnectionLifecycle, DISABLED_BackendConnectCompletesAndLinksPeers) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
  testing::StubBackend backend("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  ASSERT_NE(backend.port(), 0);
  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  const ConnectionId cid = 1;

  ConnectionId connected = kInvalidConnectionId;
  f.mgr->set_writable_handler([&](net::Connection& c) {
    if (c.role() == net::ConnectionRole::Backend)
      connected = c.id();
  });
  auto pending = net::socket_ops::connect_tcp("127.0.0.1", backend.port());
  ASSERT_TRUE(pending);
  auto bid = f.mgr->adopt_backend(std::move(pending->first), cid);
  ASSERT_TRUE(bid) << bid.error().to_string();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return connected == *bid; }));
  net::Connection* b = f.mgr->find(*bid);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->role(), net::ConnectionRole::Backend);
  EXPECT_EQ(b->state(), net::ConnectionState::ForwardingRequest);
  EXPECT_EQ(b->peer_id(), cid);
  EXPECT_EQ(f.mgr->find(cid)->peer_id(), *bid);
  EXPECT_EQ(f.mgr->stats().total_backend_connects, 1U);
}

TEST(ConnectionLifecycle, DISABLED_BackendConnectRefusedReportsErrorOnClose) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  // Port 1 on loopback: nothing listens there → ECONNREFUSED on the async connect.
  bool closed_with_error = false;
  f.mgr->set_close_handler([&](net::Connection& c) {
    if (c.role() == net::ConnectionRole::Backend) {
      closed_with_error = c.last_error().has_value() &&
                          c.last_error()->code == ErrorCode::SystemError &&
                          c.state() == net::ConnectionState::Closed;
    }
  });
  auto pending = net::socket_ops::connect_tcp("127.0.0.1", 1);
  ASSERT_TRUE(pending);
  auto bid = f.mgr->adopt_backend(std::move(pending->first), 1);
  ASSERT_TRUE(bid);
  ASSERT_TRUE(pump_until(*f.loop, [&] { return closed_with_error; }));
}

TEST(ConnectionLifecycle, DISABLED_PauseReadsStopsDeliveryUntilResumed) {
  ProxyFixture f;
  ASSERT_TRUE(f.ok());
  int deliveries = 0;
  f.mgr->set_data_handler([&](net::Connection& c) {
    ++deliveries;
    c.inbound().clear();
  });
  net::SocketHandle client = f.connect_client();
  ASSERT_TRUE(pump_until(*f.loop, [&] { return f.mgr->size() == 1; }));
  ASSERT_TRUE(f.mgr->pause_reads(1));
  EXPECT_TRUE(f.mgr->find(1)->reads_paused());
  ASSERT_EQ(::send(client.get(), "data", 4, 0), 4);
  (void)pump_until(*f.loop, [] { return false; }, 5);
  EXPECT_EQ(deliveries, 0) << "paused connection must not deliver";
  ASSERT_TRUE(f.mgr->resume_reads(1));
  ASSERT_TRUE(pump_until(*f.loop, [&] { return deliveries == 1; }))
      << "level-triggered epoll re-reports the pending bytes";
}

}  // namespace aegis
