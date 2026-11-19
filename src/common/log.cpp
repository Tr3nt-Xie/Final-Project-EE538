#include "aegisproxy/common/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace aegis::log {

namespace {
std::atomic<Level> g_level{Level::Info};
}  // namespace

void set_level(Level level) noexcept {
  g_level.store(level, std::memory_order_relaxed);
}

Level level() noexcept {
  return g_level.load(std::memory_order_relaxed);
}

std::string_view to_string(Level level) noexcept {
  switch (level) {
    case Level::Debug:
      return "DEBUG";
    case Level::Info:
      return "INFO";
    case Level::Warn:
      return "WARN";
    case Level::Error:
      return "ERROR";
    case Level::Off:
      return "OFF";
  }
  return "?";
}

void write(Level lvl, std::string_view component, std::string_view message) {
  if (lvl < level() || lvl == Level::Off) {
    return;
  }
  const auto now = std::chrono::system_clock::now();
  const std::time_t secs = std::chrono::system_clock::to_time_t(now);
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &secs);
#else
  localtime_r(&secs, &tm_buf);
#endif
  char stamp[32];
  const std::size_t n = std::strftime(stamp, sizeof stamp, "%H:%M:%S", &tm_buf);
  stamp[n] = '\0';

  std::string line;
  line.reserve(64 + component.size() + message.size());
  line += stamp;
  line += '.';
  if (millis < 100)
    line += '0';
  if (millis < 10)
    line += '0';
  line += std::to_string(millis);
  line += ' ';
  line += to_string(lvl);
  line += " [";
  line += component;
  line += "] ";
  line += message;
  line += '\n';
  std::fwrite(line.data(), 1, line.size(), stderr);
}

}  // namespace aegis::log
