#include "aegisproxy/common/config.hpp"

#include <gtest/gtest.h>

namespace aegis {

constexpr const char* kGood = R"(
# comment
listen_host = 127.0.0.1
listen_port = 8081
strategy = least_connections
max_connections = 64
buffer_limit_bytes = 4096   # trailing comment

[backend]
name = a
host = 10.0.0.1
port = 9001
weight = 3

[backend]
name = b
host = 10.0.0.2
port = 9002
)";

TEST(Config, ParsesGlobalsAndBackends) {
  auto cfg = parse_config_text(kGood);
  ASSERT_TRUE(cfg) << cfg.error().to_string();
  EXPECT_EQ(cfg->listen_host, "127.0.0.1");
  EXPECT_EQ(cfg->listen_port, 8081);
  EXPECT_EQ(cfg->strategy, "least_connections");
  EXPECT_EQ(cfg->max_connections, 64U);
  EXPECT_EQ(cfg->buffer_limit_bytes, 4096U);
  ASSERT_EQ(cfg->backends.size(), 2U);
  EXPECT_EQ(cfg->backends[0].name, "a");
  EXPECT_EQ(cfg->backends[0].port, 9001);
  EXPECT_EQ(cfg->backends[0].weight, 3U);
  EXPECT_EQ(cfg->backends[1].weight, 1U);         // default
  EXPECT_EQ(cfg->health_interval.count(), 2000);  // default kept
}

TEST(Config, RejectsUnknownKeyWithLineNumber) {
  auto cfg = parse_config_text("listen_port = 80\nbogus = 1\n[backend]\nhost=1.2.3.4\nport=1\n");
  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error().code, ErrorCode::ConfigError);
  EXPECT_NE(cfg.error().message.find("line 2"), std::string::npos);
  EXPECT_NE(cfg.error().message.find("bogus"), std::string::npos);
}

TEST(Config, RejectsMissingBackend) {
  auto cfg = parse_config_text("listen_port = 80\n");
  ASSERT_FALSE(cfg);
  EXPECT_NE(cfg.error().message.find("[backend]"), std::string::npos);
}

TEST(Config, RejectsBadInteger) {
  auto cfg = parse_config_text("listen_port = eighty\n[backend]\nhost=1.2.3.4\nport=1\n");
  ASSERT_FALSE(cfg);
  EXPECT_NE(cfg.error().message.find("listen_port"), std::string::npos);
}

TEST(Config, RejectsUnknownStrategy) {
  auto cfg = parse_config_text("strategy = magic\n[backend]\nhost=1.2.3.4\nport=1\n");
  ASSERT_FALSE(cfg);
  EXPECT_NE(cfg.error().message.find("magic"), std::string::npos);
}

TEST(Config, RejectsBackendWithoutPort) {
  auto cfg = parse_config_text("[backend]\nhost=1.2.3.4\n");
  ASSERT_FALSE(cfg);
  EXPECT_NE(cfg.error().message.find("port"), std::string::npos);
}

TEST(Config, MissingFileIsAnError) {
  auto cfg = load_config_file("/nonexistent/aegis.conf");
  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error().code, ErrorCode::ConfigError);
}

}  // namespace aegis
