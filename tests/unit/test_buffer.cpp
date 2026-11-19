#include "aegisproxy/network/buffer.hpp"

#include <cstring>
#include <string>

#include <gtest/gtest.h>

namespace aegis::net {

TEST(Buffer, StartsEmpty) {
  Buffer b;
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(b.readable_bytes(), 0U);
  EXPECT_EQ(b.max_capacity(), Buffer::kDefaultMaxCapacity);
}

TEST(Buffer, AppendThenConsume) {
  Buffer b;
  ASSERT_TRUE(b.append("hello "));
  ASSERT_TRUE(b.append("world"));
  EXPECT_EQ(b.view(), "hello world");
  b.consume(6);
  EXPECT_EQ(b.view(), "world");
  b.consume(100);  // over-consume clamps
  EXPECT_TRUE(b.empty());
}

TEST(Buffer, PrepareCommitCycle) {
  Buffer b(8, 64);
  auto region = b.prepare(5);
  ASSERT_TRUE(region);
  ASSERT_GE(region->size(), 5U);
  std::memcpy(region->data(), "abcde", 5);
  b.commit(5);
  EXPECT_EQ(b.view(), "abcde");
}

TEST(Buffer, GrowsUpToMaxCapacity) {
  Buffer b(4, 16);
  ASSERT_TRUE(b.append("0123456789"));  // > initial 4, grows
  EXPECT_EQ(b.readable_bytes(), 10U);
  EXPECT_LE(b.capacity(), 16U);
  ASSERT_TRUE(b.append("abcdef"));  // exactly 16
  EXPECT_EQ(b.readable_bytes(), 16U);
  Status st = b.append("x");
  ASSERT_FALSE(st);
  EXPECT_EQ(st.error().code, ErrorCode::CapacityExceeded);
  EXPECT_EQ(b.view(), "0123456789abcdef");  // failed append left data intact
}

TEST(Buffer, CompactionReclaimsConsumedSpace) {
  Buffer b(8, 8);
  ASSERT_TRUE(b.append("12345678"));
  b.consume(6);
  EXPECT_EQ(b.view(), "78");
  ASSERT_TRUE(b.append("abcdef"));  // needs compaction, no growth possible
  EXPECT_EQ(b.view(), "78abcdef");
  EXPECT_EQ(b.capacity(), 8U);
}

TEST(Buffer, ClearResets) {
  Buffer b;
  ASSERT_TRUE(b.append("data"));
  b.clear();
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(b.remaining_capacity(), b.max_capacity());
}

TEST(Buffer, MoveTransfersContents) {
  Buffer a;
  ASSERT_TRUE(a.append("moved"));
  Buffer b(std::move(a));
  EXPECT_EQ(b.view(), "moved");
}

}  // namespace aegis::net
