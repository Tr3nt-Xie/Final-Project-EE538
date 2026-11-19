#include "aegisproxy/common/result.hpp"

#include <string>

#include <gtest/gtest.h>

namespace aegis {

TEST(Result, HoldsValue) {
  Result<int> r = 42;
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(static_cast<bool>(r));
  EXPECT_EQ(*r, 42);
  EXPECT_EQ(r.value_or(7), 42);
}

TEST(Result, HoldsError) {
  Result<int> r = Error{ErrorCode::NotFound, "nope"};
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::NotFound);
  EXPECT_EQ(r.error().message, "nope");
  EXPECT_EQ(r.value_or(7), 7);
}

TEST(Result, MoveOnlyValue) {
  struct MoveOnly {
    std::string s;
    MoveOnly(std::string v) : s(std::move(v)) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) = default;
  };
  Result<MoveOnly> r = MoveOnly{"x"};
  MoveOnly m = std::move(r).value();
  EXPECT_EQ(m.s, "x");
}

TEST(Status, OkAndError) {
  Status s = ok();
  EXPECT_TRUE(s);
  Status e = Error::not_implemented("Thing");
  EXPECT_FALSE(e);
  EXPECT_EQ(e.error().code, ErrorCode::NotImplemented);
  EXPECT_NE(e.error().message.find("Thing"), std::string::npos);
}

TEST(Error, FromErrnoCarriesCodeAndText) {
  Error e = Error::from_errno("open", EACCES);
  EXPECT_EQ(e.code, ErrorCode::SystemError);
  EXPECT_EQ(e.sys_errno, EACCES);
  const std::string text = e.to_string();
  EXPECT_NE(text.find("SystemError"), std::string::npos);
  EXPECT_NE(text.find("open"), std::string::npos);
  EXPECT_NE(text.find("errno 13"), std::string::npos);
}

TEST(ErrorCode, EveryCodeHasAName) {
  for (int i = 0; i <= static_cast<int>(ErrorCode::SystemError); ++i) {
    EXPECT_NE(to_string(static_cast<ErrorCode>(i)), "Unknown");
  }
}

}  // namespace aegis
