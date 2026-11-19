#include "aegisproxy/common/config.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace aegis {

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

template <typename Int>
Result<Int> parse_int(std::string_view value, std::size_t line, std::string_view key) {
  Int out{};
  const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
  if (ec != std::errc{} || ptr != value.data() + value.size()) {
    return Error{ErrorCode::ConfigError, "line " + std::to_string(line) + ": '" + std::string(key) +
                                             "' expects an integer, got '" + std::string(value) +
                                             "'"};
  }
  return out;
}

Status apply_global(ProxyConfig& cfg, std::string_view key, std::string_view value,
                    std::size_t line) {
  if (key == "listen_host") {
    cfg.listen_host = value;
  } else if (key == "listen_port") {
    auto v = parse_int<std::uint16_t>(value, line, key);
    if (!v)
      return v.error();
    cfg.listen_port = *v;
  } else if (key == "strategy") {
    cfg.strategy = value;
  } else if (key == "max_connections") {
    auto v = parse_int<std::size_t>(value, line, key);
    if (!v)
      return v.error();
    cfg.max_connections = *v;
  } else if (key == "buffer_limit_bytes") {
    auto v = parse_int<std::size_t>(value, line, key);
    if (!v)
      return v.error();
    cfg.buffer_limit_bytes = *v;
  } else if (key == "listen_backlog") {
    auto v = parse_int<int>(value, line, key);
    if (!v)
      return v.error();
    cfg.listen_backlog = *v;
  } else if (key == "log_level") {
    cfg.log_level = value;
  } else if (key == "health_interval_ms") {
    auto v = parse_int<long>(value, line, key);
    if (!v)
      return v.error();
    cfg.health_interval = std::chrono::milliseconds{*v};
  } else if (key == "health_timeout_ms") {
    auto v = parse_int<long>(value, line, key);
    if (!v)
      return v.error();
    cfg.health_timeout = std::chrono::milliseconds{*v};
  } else if (key == "healthy_threshold") {
    auto v = parse_int<unsigned>(value, line, key);
    if (!v)
      return v.error();
    cfg.healthy_threshold = *v;
  } else if (key == "unhealthy_threshold") {
    auto v = parse_int<unsigned>(value, line, key);
    if (!v)
      return v.error();
    cfg.unhealthy_threshold = *v;
  } else if (key == "health_path") {
    cfg.health_path = value;
  } else {
    return Error{ErrorCode::ConfigError,
                 "line " + std::to_string(line) + ": unknown key '" + std::string(key) + "'"};
  }
  return ok();
}

Status apply_backend(BackendConfig& backend, std::string_view key, std::string_view value,
                     std::size_t line) {
  if (key == "name") {
    backend.name = value;
  } else if (key == "host") {
    backend.host = value;
  } else if (key == "port") {
    auto v = parse_int<std::uint16_t>(value, line, key);
    if (!v)
      return v.error();
    backend.port = *v;
  } else if (key == "weight") {
    auto v = parse_int<std::uint32_t>(value, line, key);
    if (!v)
      return v.error();
    backend.weight = *v;
  } else {
    return Error{ErrorCode::ConfigError, "line " + std::to_string(line) +
                                             ": unknown backend key '" + std::string(key) + "'"};
  }
  return ok();
}

}  // namespace

Result<ProxyConfig> parse_config_text(std::string_view text) {
  ProxyConfig cfg;
  bool in_backend = false;
  BackendConfig current;
  std::size_t line_no = 0;

  auto flush_backend = [&]() {
    if (in_backend) {
      cfg.backends.push_back(std::move(current));
      current = BackendConfig{};
    }
  };

  std::size_t pos = 0;
  while (pos <= text.size()) {
    const std::size_t nl = text.find('\n', pos);
    std::string_view raw =
        text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
    ++line_no;

    const std::size_t hash = raw.find('#');
    if (hash != std::string_view::npos)
      raw = raw.substr(0, hash);
    const std::string_view line = trim(raw);
    if (line.empty())
      continue;

    if (line.front() == '[') {
      if (line.back() != ']') {
        return Error{ErrorCode::ConfigError,
                     "line " + std::to_string(line_no) + ": unterminated section header"};
      }
      const std::string_view section = trim(line.substr(1, line.size() - 2));
      if (section != "backend") {
        return Error{
            ErrorCode::ConfigError,
            "line " + std::to_string(line_no) + ": unknown section '" + std::string(section) + "'"};
      }
      flush_backend();
      in_backend = true;
      continue;
    }

    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      return Error{ErrorCode::ConfigError,
                   "line " + std::to_string(line_no) + ": expected 'key = value'"};
    }
    const std::string_view key = trim(line.substr(0, eq));
    const std::string_view value = trim(line.substr(eq + 1));
    if (key.empty()) {
      return Error{ErrorCode::ConfigError, "line " + std::to_string(line_no) + ": empty key"};
    }

    const Status st = in_backend ? apply_backend(current, key, value, line_no)
                                 : apply_global(cfg, key, value, line_no);
    if (!st)
      return st.error();
  }
  flush_backend();

  if (Status st = validate_config(cfg); !st)
    return st.error();
  return cfg;
}

Result<ProxyConfig> load_config_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Error{ErrorCode::ConfigError, "cannot open config file '" + path + "'"};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_config_text(ss.str());
}

Status validate_config(const ProxyConfig& cfg) {
  if (cfg.listen_port == 0) {
    return Error{ErrorCode::ConfigError, "listen_port must be in 1..65535"};
  }
  if (cfg.strategy != "round_robin" && cfg.strategy != "least_connections" &&
      cfg.strategy != "adaptive") {
    return Error{ErrorCode::ConfigError, "unknown strategy '" + cfg.strategy +
                                             "' (round_robin | least_connections | adaptive)"};
  }
  if (cfg.max_connections == 0) {
    return Error{ErrorCode::ConfigError, "max_connections must be > 0"};
  }
  if (cfg.buffer_limit_bytes < 1024) {
    return Error{ErrorCode::ConfigError, "buffer_limit_bytes must be >= 1024"};
  }
  if (cfg.listen_backlog <= 0) {
    return Error{ErrorCode::ConfigError, "listen_backlog must be > 0"};
  }
  if (cfg.log_level != "debug" && cfg.log_level != "info" && cfg.log_level != "warn" &&
      cfg.log_level != "error") {
    return Error{ErrorCode::ConfigError, "log_level must be debug | info | warn | error"};
  }
  if (cfg.health_interval.count() <= 0 || cfg.health_timeout.count() <= 0) {
    return Error{ErrorCode::ConfigError, "health_interval_ms and health_timeout_ms must be > 0"};
  }
  if (cfg.healthy_threshold == 0 || cfg.unhealthy_threshold == 0) {
    return Error{ErrorCode::ConfigError, "healthy/unhealthy thresholds must be >= 1"};
  }
  if (cfg.backends.empty()) {
    return Error{ErrorCode::ConfigError, "at least one [backend] section is required"};
  }
  for (std::size_t i = 0; i < cfg.backends.size(); ++i) {
    const auto& b = cfg.backends[i];
    if (b.host.empty()) {
      return Error{ErrorCode::ConfigError,
                   "backend #" + std::to_string(i + 1) + ": host is required"};
    }
    if (b.port == 0) {
      return Error{ErrorCode::ConfigError,
                   "backend #" + std::to_string(i + 1) + ": port must be in 1..65535"};
    }
    if (b.weight == 0) {
      return Error{ErrorCode::ConfigError,
                   "backend #" + std::to_string(i + 1) + ": weight must be >= 1"};
    }
  }
  return ok();
}

}  // namespace aegis
