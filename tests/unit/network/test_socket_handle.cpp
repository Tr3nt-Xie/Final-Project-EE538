#include "aegisproxy/network/socket_handle.hpp"

#include <cerrno>
#include <utility>

#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aegis::net {

namespace {
bool fd_is_open(Fd fd) {
  return ::fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}
}  // namespace

TEST(SocketHandle, DefaultIsInvalid) {
  SocketHandle h;
  EXPECT_FALSE(h.valid());
  EXPECT_EQ(h.get(), kInvalidFd);
}

TEST(SocketHandle, ClosesOnDestruction) {
  const Fd raw = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(raw, 0);
  {
    SocketHandle h{raw};
    EXPECT_TRUE(h.valid());
    EXPECT_TRUE(fd_is_open(raw));
  }
  EXPECT_FALSE(fd_is_open(raw));
}

TEST(SocketHandle, MoveConstructTransfersOwnership) {
  const Fd raw = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(raw, 0);
  SocketHandle a{raw};
  SocketHandle b{std::move(a)};
  EXPECT_FALSE(a.valid());  // NOLINT(bugprone-use-after-move): testing moved-from state
  EXPECT_EQ(b.get(), raw);
  EXPECT_TRUE(fd_is_open(raw));
}

TEST(SocketHandle, MoveAssignClosesPreviousFd) {
  const Fd first = ::socket(AF_INET, SOCK_STREAM, 0);
  const Fd second = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(first, 0);
  ASSERT_GE(second, 0);
  SocketHandle a{first};
  SocketHandle b{second};
  a = std::move(b);
  EXPECT_FALSE(fd_is_open(first));  // overwritten handle closed its fd
  EXPECT_TRUE(fd_is_open(second));
  EXPECT_EQ(a.get(), second);
  EXPECT_FALSE(b.valid());  // NOLINT(bugprone-use-after-move)
}

TEST(SocketHandle, SelfMoveAssignIsSafe) {
  const Fd raw = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(raw, 0);
  SocketHandle a{raw};
  SocketHandle& alias = a;
  a = std::move(alias);
  EXPECT_EQ(a.get(), raw);
  EXPECT_TRUE(fd_is_open(raw));
}

TEST(SocketHandle, ReleaseGivesUpOwnership) {
  const Fd raw = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(raw, 0);
  Fd released = kInvalidFd;
  {
    SocketHandle h{raw};
    released = h.release();
    EXPECT_FALSE(h.valid());
  }
  EXPECT_EQ(released, raw);
  EXPECT_TRUE(fd_is_open(raw));
  ::close(raw);
}

TEST(SocketHandle, ResetReplacesAndCloses) {
  const Fd first = ::socket(AF_INET, SOCK_STREAM, 0);
  const Fd second = ::socket(AF_INET, SOCK_STREAM, 0);
  SocketHandle h{first};
  h.reset(second);
  EXPECT_FALSE(fd_is_open(first));
  EXPECT_EQ(h.get(), second);
  h.reset();
  EXPECT_FALSE(h.valid());
  EXPECT_FALSE(fd_is_open(second));
}

TEST(SocketOps, CreatedSocketIsNonBlockingAndCloexec) {
  auto sock = socket_ops::create_tcp_socket();
  ASSERT_TRUE(sock) << sock.error().to_string();
  const int fl = ::fcntl(sock->get(), F_GETFL);
  const int fd_flags = ::fcntl(sock->get(), F_GETFD);
  EXPECT_NE(fl & O_NONBLOCK, 0);
  EXPECT_NE(fd_flags & FD_CLOEXEC, 0);
}

TEST(SocketOps, ListenOnEphemeralPortReportsPort) {
  auto listener = socket_ops::listen_tcp("127.0.0.1", 0, 8);
  ASSERT_TRUE(listener) << listener.error().to_string();
  auto port = socket_ops::local_port(listener->get());
  ASSERT_TRUE(port);
  EXPECT_GT(*port, 0);
}

TEST(SocketOps, RejectsHostnamesUntilResolverExists) {
  auto listener = socket_ops::listen_tcp("localhost", 0, 8);
  ASSERT_FALSE(listener);
  EXPECT_EQ(listener.error().code, ErrorCode::InvalidArgument);
}

TEST(SocketOps, AcceptOnIdleListenerReturnsNullopt) {
  auto listener = socket_ops::listen_tcp("127.0.0.1", 0, 8);
  ASSERT_TRUE(listener);
  auto accepted = socket_ops::accept_tcp(listener->get());
  ASSERT_TRUE(accepted) << accepted.error().to_string();
  EXPECT_FALSE(accepted->has_value());
}

}  // namespace aegis::net
