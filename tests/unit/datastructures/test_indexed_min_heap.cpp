#include "aegisproxy/datastructures/indexed_min_heap.hpp"

#include <algorithm>
#include <random>
#include <vector>

#include <gtest/gtest.h>

namespace aegis::ds {

using Heap = IndexedMinHeap<int, int>;

// ---- Implemented now ----

TEST(IndexedMinHeap, StartsEmptyWithValidInvariants) {
  Heap h;
  EXPECT_TRUE(h.empty());
  EXPECT_EQ(h.size(), 0U);
  EXPECT_TRUE(h.check_invariants());
}

TEST(IndexedMinHeap, UnimplementedOperationsReportNotImplemented) {
  Heap h;
  EXPECT_EQ(h.insert(1, 10).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(h.remove(1).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(h.update_key(1, 5).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(h.peek_min().error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(h.extract_min().error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(h.contains(1).error().code, ErrorCode::NotImplemented);
  EXPECT_TRUE(h.empty()) << "a failed insert must not change the heap";
}

// ---- Pending (Member 3): remove the DISABLED_ prefix as each operation lands ----

TEST(IndexedMinHeap, DISABLED_InsertMaintainsInvariantsAndMin) {
  Heap h;
  const std::vector<int> prios = {5, 3, 8, 1, 9, 2, 7};
  for (int i = 0; i < static_cast<int>(prios.size()); ++i) {
    ASSERT_TRUE(h.insert(i, prios[static_cast<std::size_t>(i)]));
    ASSERT_TRUE(h.check_invariants());
  }
  EXPECT_EQ(h.size(), prios.size());
  auto min = h.peek_min();
  ASSERT_TRUE(min);
  EXPECT_EQ(min->priority, 1);
  EXPECT_EQ(min->key, 3);
}

TEST(IndexedMinHeap, DISABLED_InsertDuplicateKeyIsRejected) {
  Heap h;
  ASSERT_TRUE(h.insert(1, 10));
  EXPECT_EQ(h.insert(1, 20).error().code, ErrorCode::AlreadyExists);
  EXPECT_EQ(h.size(), 1U);
}

TEST(IndexedMinHeap, DISABLED_ExtractMinYieldsSortedOrder) {
  Heap h;
  std::mt19937 rng(7);
  std::vector<int> prios(200);
  for (int i = 0; i < 200; ++i) {
    prios[static_cast<std::size_t>(i)] = static_cast<int>(rng() % 1000);
    ASSERT_TRUE(h.insert(i, prios[static_cast<std::size_t>(i)]));
  }
  std::sort(prios.begin(), prios.end());
  for (int expected : prios) {
    auto e = h.extract_min();
    ASSERT_TRUE(e);
    EXPECT_EQ(e->priority, expected);
    ASSERT_TRUE(h.check_invariants());
  }
  EXPECT_TRUE(h.empty());
  EXPECT_EQ(h.peek_min().error().code, ErrorCode::NotFound);
}

TEST(IndexedMinHeap, DISABLED_UpdateKeyMovesBothDirections) {
  Heap h;
  for (int i = 0; i < 10; ++i)
    ASSERT_TRUE(h.insert(i, i * 10));
  ASSERT_TRUE(h.update_key(9, -1));  // decrease: must become the new min
  EXPECT_EQ(h.peek_min()->key, 9);
  ASSERT_TRUE(h.check_invariants());
  ASSERT_TRUE(h.update_key(9, 1000));  // increase: must sink to the bottom
  EXPECT_EQ(h.peek_min()->key, 0);
  ASSERT_TRUE(h.check_invariants());
  EXPECT_EQ(h.update_key(42, 0).error().code, ErrorCode::NotFound);
}

TEST(IndexedMinHeap, DISABLED_RemoveArbitraryKey) {
  Heap h;
  for (int i = 0; i < 20; ++i)
    ASSERT_TRUE(h.insert(i, 20 - i));
  ASSERT_TRUE(h.remove(10));
  EXPECT_FALSE(*h.contains(10));
  EXPECT_EQ(h.size(), 19U);
  ASSERT_TRUE(h.check_invariants());
  EXPECT_EQ(h.remove(10).error().code, ErrorCode::NotFound);
}

}  // namespace aegis::ds
