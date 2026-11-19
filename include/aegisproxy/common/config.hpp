#pragma once

#include "aegisproxy/common/result.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// Proxy configuration and its parser.
///
/// File format (see config/aegisproxy.example.conf): `key = value` lines, `#` comments,
/// and one `[backend]` section per upstream server. Unknown keys are errors so that a
/// typo cannot silently disable a feature.
namespace aegis {

struct BackendConfig {
  std::string name;
  std::string host;
  std::uint16_t port{0};
  std::uint32_t weight{1};
};

struct ProxyConfig {
  std::string listen_host{"0.0.0.0"};
  std::uint16_t listen_port{8080};
  std::string strategy{"round_robin"};  ///< round_robin | least_connections | adaptive
  std::size_t max_connections{1024};
  std::size_t buffer_limit_bytes{64 * 1024};
  int listen_backlog{128};
  std::string log_level{"info"};

  std::chrono::milliseconds health_interval{2000};
  std::chrono::milliseconds health_timeout{500};
  unsigned healthy_threshold{2};
  unsigned unhealthy_threshold{3};
  std::string health_path{"/health"};

  std::vector<BackendConfig> backends;
};

/// Parses configuration text. Errors carry "line N: ..." messages.
[[nodiscard]] Result<ProxyConfig> parse_config_text(std::string_view text);

/// Reads and parses a configuration file.
[[nodiscard]] Result<ProxyConfig> load_config_file(const std::string& path);

/// Semantic validation (ports, thresholds, at least one backend, known strategy).
[[nodiscard]] Status validate_config(const ProxyConfig& config);

}  // namespace aegis
