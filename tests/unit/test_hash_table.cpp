#include "aegisproxy/datastructures/hash_table.hpp"

#include <string>

#include <gtest/gtest.h>

namespace aegis::ds {

using Table = HashTable<std::string, int>;

/// Every key lands in bucket 0 — forces collisions for chaining tests.
struct ConstantHash {
  std::size_t operator()(const std::string&) const noexcept { return 0; }
};
using CollidingTable = HashTable<std::string, int, ConstantHash>;

// ---- Implemented now ----

TEST(HashTable, StartsEmptyWithDefaultBuckets) {
  Table t;
  EXPECT_TRUE(t.empty());
  EXPECT_EQ(t.size(), 0U);
  EXPECT_EQ(t.bucket_count(), Table::kDefaultBucketCount);
  EXPECT_FLOAT_EQ(t.load_factor(), 0.0F);
  EXPECT_TRUE(t.check_invariants());
}

TEST(HashTable, ZeroBucketCountFallsBackToDefault) {
  Table t(0);
  EXPECT_EQ(t.bucket_count(), Table::kDefaultBucketCount);
}

TEST(HashTable, UnimplementedOperationsReportNotImplemented) {
  Table t;
  EXPECT_EQ(t.insert("a", 1).error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(t.find("a").error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(t.erase("a").error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(t.contains("a").error().code, ErrorCode::NotImplemented);
  EXPECT_EQ(t.rehash(64).error().code, ErrorCode::NotImplemented);
  EXPECT_TRUE(t.empty());
}

// ---- Pending (Member 3) ----

TEST(HashTable, DISABLED_InsertFindErase) {
  Table t;
  EXPECT_TRUE(*t.insert("one", 1));
  EXPECT_TRUE(*t.insert("two", 2));
  EXPECT_FALSE(*t.insert("one", 11)) << "overwrite returns false";
  EXPECT_EQ(t.size(), 2U);
  ASSERT_NE(*t.find("one"), nullptr);
  EXPECT_EQ(**t.find("one"), 11);
  EXPECT_EQ(*t.find("three"), nullptr);
  EXPECT_TRUE(*t.contains("two"));
  EXPECT_TRUE(*t.erase("two"));
  EXPECT_FALSE(*t.erase("two"));
  EXPECT_FALSE(*t.contains("two"));
  EXPECT_EQ(t.size(), 1U);
  EXPECT_TRUE(t.check_invariants());
}

TEST(HashTable, DISABLED_CollisionsAreChainedNotLost) {
  CollidingTable t(4);
  t.set_max_load_factor(100.0F);  // never rehash in this test
  for (int i = 0; i < 10; ++i)
    ASSERT_TRUE(t.insert("k" + std::to_string(i), i));
  EXPECT_EQ(t.size(), 10U);
  EXPECT_EQ(t.chain_length("k0"), 10U);
  for (int i = 0; i < 10; ++i) {
    auto v = t.find("k" + std::to_string(i));
    ASSERT_TRUE(v);
    ASSERT_NE(*v, nullptr);
    EXPECT_EQ(**v, i);
  }
  ASSERT_TRUE(*t.erase("k5"));
  EXPECT_EQ(*t.find("k5"), nullptr);
  EXPECT_EQ(**t.find("k9"), 9) << "erasing from the middle of a chain keeps the rest";
  EXPECT_TRUE(t.check_invariants());
}

TEST(HashTable, DISABLED_ResizesWhenLoadFactorExceeded) {
  Table t(4);
  for (int i = 0; i < 100; ++i)
    ASSERT_TRUE(t.insert(std::to_string(i), i));
  EXPECT_GT(t.bucket_count(), 4U);
  EXPECT_LE(t.load_factor(), t.max_load_factor());
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(**t.find(std::to_string(i)), i);
  EXPECT_TRUE(t.check_invariants());
}

TEST(HashTable, DISABLED_ExplicitRehashPreservesContents) {
  Table t(8);
  for (int i = 0; i < 20; ++i)
    ASSERT_TRUE(t.insert(std::to_string(i), i));
  ASSERT_TRUE(t.rehash(128));
  EXPECT_GE(t.bucket_count(), 128U);
  EXPECT_EQ(t.size(), 20U);
  for (int i = 0; i < 20; ++i)
    EXPECT_EQ(**t.find(std::to_string(i)), i);
  EXPECT_TRUE(t.check_invariants());
}

}  // namespace aegis::ds
