#pragma once

#include <cstdint>
#include <string_view>

/// Minimal stderr logger. Deliberately tiny: no sinks, no formatting engine.
/// Callers format with std::format and pass the finished string.
///
/// Thread-safety: the level is atomic; writes are single `fwrite` calls to stderr.
/// The process-wide level is the one intentional piece of mutable global state in
/// the project (a logger that had to be threaded through every constructor would
/// hurt readability more than it helps).
namespace aegis::log {

enum class Level : std::uint8_t { Debug = 0, Info, Warn, Error, Off };

void set_level(Level level) noexcept;
[[nodiscard]] Level level() noexcept;
[[nodiscard]] std::string_view to_string(Level level) noexcept;

/// Writes "<time> <LEVEL> [component] message\n" if level >= the configured level.
void write(Level level, std::string_view component, std::string_view message);

inline void debug(std::string_view component, std::string_view message) {
  write(Level::Debug, component, message);
}
inline void info(std::string_view component, std::string_view message) {
  write(Level::Info, component, message);
}
inline void warn(std::string_view component, std::string_view message) {
  write(Level::Warn, component, message);
}
inline void error(std::string_view component, std::string_view message) {
  write(Level::Error, component, message);
}

}  // namespace aegis::log
