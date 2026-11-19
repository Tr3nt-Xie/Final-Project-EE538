#include "aegisproxy/network/event_loop.hpp"

#include <chrono>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <unistd.h>

#include "fake_poller.hpp"
#if defined(AEGIS_HAVE_EPOLL)
#include "aegisproxy/network/epoll_poller.hpp"
#endif

namespace aegis::net {

using namespace std::chrono_literals;

class EventLoopFakeTest : public ::testing::Test {
 protected:
  EventLoopFakeTest() {
    auto fake = std::make_unique<testing::FakePoller>();
    poller = fake.get();
    loop = std::make_unique<EventLoop>(std::move(fake), 8);
  }
  testing::FakePoller* poller{nullptr};
  std::unique_ptr<EventLoop> loop;
  // Fds 100..199 are never real; FakePoller never touches them.
  static constexpr Fd kFd = 100;
};

TEST_F(EventLoopFakeTest, AddRegistersWithPollerAndIsVisible) {
  const std::size_t before = loop->handler_count();  // wake pipe, if it installed
  auto id = loop->add(kFd, EventMask::Readable, [](EventMask) {});
  ASSERT_TRUE(id) << id.error().to_string();
  EXPECT_TRUE(loop->contains(*id));
  EXPECT_EQ(loop->handler_count(), before + 1);
  ASSERT_TRUE(poller->registered.contains(kFd));
  EXPECT_EQ(poller->registered[kFd].token, *id);
  EXPECT_EQ(poller->registered[kFd].interest, EventMask::Readable);
}

TEST_F(EventLoopFakeTest, AddRejectsInvalidInput) {
  EXPECT_EQ(loop->add(kInvalidFd, EventMask::Readable, [](EventMask) {}).error().code,
            ErrorCode::InvalidArgument);
  EXPECT_EQ(loop->add(kFd, EventMask::Readable, nullptr).error().code, ErrorCode::InvalidArgument);
}

TEST_F(EventLoopFakeTest, ModifyUpdatesInterest) {
  auto id = loop->add(kFd, EventMask::Readable, [](EventMask) {});
  ASSERT_TRUE(id);
  ASSERT_TRUE(loop->modify(*id, EventMask::Readable | EventMask::Writable));
  EXPECT_EQ(poller->registered[kFd].interest, EventMask::Readable | EventMask::Writable);
  EXPECT_EQ(loop->modify(9999, EventMask::Readable).error().code, ErrorCode::NotFound);
}

TEST_F(EventLoopFakeTest, RemoveUnregisters) {
  auto id = loop->add(kFd, EventMask::Readable, [](EventMask) {});
  ASSERT_TRUE(id);
  ASSERT_TRUE(loop->remove(*id));
  EXPECT_FALSE(loop->contains(*id));
  EXPECT_FALSE(poller->registered.contains(kFd));
  EXPECT_EQ(loop->remove(*id).error().code, ErrorCode::NotFound);  // not idempotent by design
}

TEST_F(EventLoopFakeTest, HandlerIdsAreNeverReused) {
  auto a = loop->add(kFd, EventMask::Readable, [](EventMask) {});
  ASSERT_TRUE(loop->remove(*a));
  auto b = loop->add(kFd, EventMask::Readable, [](EventMask) {});
  EXPECT_NE(*a, *b);
}

TEST_F(EventLoopFakeTest, DispatchesEventsToCallback) {
  std::vector<EventMask> seen;
  auto id = loop->add(kFd, EventMask::Readable, [&](EventMask m) { seen.push_back(m); });
  poller->push(*id, EventMask::Readable);
  poller->push(*id, EventMask::Writable | EventMask::HangUp);
  auto n = loop->run_once(0ms);
  ASSERT_TRUE(n);
  EXPECT_EQ(*n, 2U);
  ASSERT_EQ(seen.size(), 2U);
  EXPECT_EQ(seen[0], EventMask::Readable);
  EXPECT_TRUE(has(seen[1], EventMask::HangUp));
}

TEST_F(EventLoopFakeTest, StaleEventsForRemovedHandlerAreDropped) {
  int calls = 0;
  auto id = loop->add(kFd, EventMask::Readable, [&](EventMask) { ++calls; });
  ASSERT_TRUE(loop->remove(*id));
  poller->push(*id, EventMask::Readable);
  auto n = loop->run_once(0ms);
  ASSERT_TRUE(n);
  EXPECT_EQ(*n, 0U);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(loop->stale_events_dropped(), 1U);
}

TEST_F(EventLoopFakeTest, CallbackMayRemoveItselfMidRound) {
  // Two events for the same handler in one round; the first callback removes the
  // handler, so the second must be dropped and nothing may crash.
  int calls = 0;
  HandlerId self = kInvalidHandlerId;
  auto id = loop->add(kFd, EventMask::Readable, [&](EventMask) {
    ++calls;
    ASSERT_TRUE(loop->remove(self));
  });
  self = *id;
  poller->push(self, EventMask::Readable);
  poller->push(self, EventMask::Readable);
  auto n = loop->run_once(0ms);
  ASSERT_TRUE(n);
  EXPECT_EQ(*n, 1U);
  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(loop->contains(self));
}

TEST_F(EventLoopFakeTest, DeferredTasksRunAfterDispatch) {
  std::vector<int> order;
  auto id = loop->add(kFd, EventMask::Readable, [&](EventMask) {
    order.push_back(1);
    loop->defer([&] { order.push_back(3); });
    order.push_back(2);
  });
  poller->push(*id, EventMask::Readable);
  ASSERT_TRUE(loop->run_once(0ms));
  EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST_F(EventLoopFakeTest, RunStopsWhenStopIsCalledFromCallback) {
  auto id = loop->add(kFd, EventMask::Readable, [&](EventMask) { loop->stop(); });
  poller->push(*id, EventMask::Readable);
  Status st = loop->run();
  EXPECT_TRUE(st) << st.error().to_string();
  EXPECT_FALSE(loop->is_running());
  // A later run() starts fresh: stop_requested is cleared, so it runs until stopped again.
  poller->push(*id, EventMask::Readable);
  EXPECT_TRUE(loop->run());
  EXPECT_EQ(poller->wait_calls, 2);
}

#if defined(AEGIS_HAVE_EPOLL)
TEST(EventLoopEpoll, CreateAndDispatchOnPipe) {
  auto loop = EventLoop::create();
  ASSERT_TRUE(loop) << loop.error().to_string();
  int fds[2];
  ASSERT_EQ(::pipe(fds), 0);
  SocketHandle rd{fds[0]};
  SocketHandle wr{fds[1]};

  EventMask got = EventMask::None;
  auto id = (*loop)->add(rd.get(), EventMask::Readable, [&](EventMask m) { got = m; });
  ASSERT_TRUE(id);

  auto n = (*loop)->run_once(0ms);
  ASSERT_TRUE(n);
  EXPECT_EQ(*n, 0U) << "nothing written yet";

  ASSERT_EQ(::write(wr.get(), "x", 1), 1);
  n = (*loop)->run_once(100ms);
  ASSERT_TRUE(n);
  EXPECT_EQ(*n, 1U);
  EXPECT_TRUE(has(got, EventMask::Readable));

  ASSERT_TRUE((*loop)->remove(*id));
  n = (*loop)->run_once(0ms);
  ASSERT_TRUE(n);
  EXPECT_EQ(*n, 0U) << "removed fd must not fire even though it is still readable";
}

TEST(EventLoopEpoll, StopWakesABlockingRun) {
  auto loop = EventLoop::create();
  ASSERT_TRUE(loop);
  // Stop before run: run() must still return promptly because the wake pipe has a byte.
  (*loop)->stop();
  const auto t0 = std::chrono::steady_clock::now();
  Status st = (*loop)->run();
  EXPECT_TRUE(st);
  EXPECT_LT(std::chrono::steady_clock::now() - t0, 2s);
}
#else
TEST(EventLoopEpoll, CreateIsUnsupportedOffLinux) {
  auto loop = EventLoop::create();
  ASSERT_FALSE(loop);
  EXPECT_EQ(loop.error().code, ErrorCode::Unsupported);
}
#endif

}  // namespace aegis::net
