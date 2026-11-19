#pragma once

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

/// Error handling conventions for AegisProxy.
///
///  * Functions that can fail return Result<T> (or Status = Result<void>).
///  * Exceptions are not used for control flow; std::bad_alloc may still propagate.
///  * Unimplemented features return ErrorCode::NotImplemented — never silent success.
///  * System-call failures carry errno in Error::sys_errno.
namespace aegis {

enum class ErrorCode : std::uint8_t {
  Ok = 0,
  NotImplemented,  ///< Scaffolded but not yet written (see TODO at the call site).
  Unsupported,     ///< Not available on this platform/build (e.g. epoll on macOS).
  InvalidArgument,
  InvalidState,  ///< Operation not valid in the object's current state.
  NotFound,
  AlreadyExists,
  CapacityExceeded,  ///< Bounded buffer / connection limit reached.
  WouldBlock,        ///< Non-blocking operation would block; retry on readiness.
  ConnectionClosed,  ///< Peer closed the connection.
  ParseError,        ///< Malformed HTTP / config input.
  ConfigError,
  NoHealthyBackend,  ///< Load balancer has nothing to select.
  SystemError,       ///< errno-carrying OS failure.
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok:
      return "Ok";
    case ErrorCode::NotImplemented:
      return "NotImplemented";
    case ErrorCode::Unsupported:
      return "Unsupported";
    case ErrorCode::InvalidArgument:
      return "InvalidArgument";
    case ErrorCode::InvalidState:
      return "InvalidState";
    case ErrorCode::NotFound:
      return "NotFound";
    case ErrorCode::AlreadyExists:
      return "AlreadyExists";
    case ErrorCode::CapacityExceeded:
      return "CapacityExceeded";
    case ErrorCode::WouldBlock:
      return "WouldBlock";
    case ErrorCode::ConnectionClosed:
      return "ConnectionClosed";
    case ErrorCode::ParseError:
      return "ParseError";
    case ErrorCode::ConfigError:
      return "ConfigError";
    case ErrorCode::NoHealthyBackend:
      return "NoHealthyBackend";
    case ErrorCode::SystemError:
      return "SystemError";
  }
  return "Unknown";
}

struct Error {
  ErrorCode code{ErrorCode::Ok};
  std::string message;
  int sys_errno{0};

  Error() = default;
  Error(ErrorCode c, std::string msg, int err = 0)
      : code(c), message(std::move(msg)), sys_errno(err) {}

  /// Builds a SystemError from the current (or given) errno.
  [[nodiscard]] static Error from_errno(std::string_view what, int err = errno);

  /// Builds the canonical NotImplemented error naming the missing feature.
  [[nodiscard]] static Error not_implemented(std::string_view feature);

  /// "Code: message (errno N: strerror)" for logs.
  [[nodiscard]] std::string to_string() const;
};

/// Result<T>: either a value of T or an Error. Modelled on std::expected (C++23),
/// which we cannot rely on with every C++20 toolchain.
template <typename T>
class [[nodiscard]] Result {
 public:
  using value_type = T;

  Result(T value)
      : storage_(std::in_place_index<0>, std::move(value)) {
  }  // NOLINT(google-explicit-constructor)
  Result(Error error)
      : storage_(std::in_place_index<1>, std::move(error)) {
  }  // NOLINT(google-explicit-constructor)

  [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] T& value() & {
    assert(has_value() && "Result::value() called on an error");
    return std::get<0>(storage_);
  }
  [[nodiscard]] const T& value() const& {
    assert(has_value() && "Result::value() called on an error");
    return std::get<0>(storage_);
  }
  [[nodiscard]] T&& value() && {
    assert(has_value() && "Result::value() called on an error");
    return std::get<0>(std::move(storage_));
  }

  [[nodiscard]] const Error& error() const& {
    assert(!has_value() && "Result::error() called on a value");
    return std::get<1>(storage_);
  }
  [[nodiscard]] Error&& error() && {
    assert(!has_value() && "Result::error() called on a value");
    return std::get<1>(std::move(storage_));
  }

  [[nodiscard]] T* operator->() { return &value(); }
  [[nodiscard]] const T* operator->() const { return &value(); }
  [[nodiscard]] T& operator*() & { return value(); }
  [[nodiscard]] const T& operator*() const& { return value(); }
  [[nodiscard]] T&& operator*() && { return std::move(*this).value(); }

  template <typename U>
  [[nodiscard]] T value_or(U&& fallback) const& {
    return has_value() ? value() : static_cast<T>(std::forward<U>(fallback));
  }

 private:
  std::variant<T, Error> storage_;
};

/// Result<void>: success, or an Error.
template <>
class [[nodiscard]] Result<void> {
 public:
  Result() = default;
  Result(Error error) : error_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

  [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }
  explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] const Error& error() const& {
    assert(error_.has_value() && "Result<void>::error() called on success");
    return *error_;
  }

 private:
  std::optional<Error> error_;
};

using Status = Result<void>;

/// Convenience for the success case of Status-returning functions.
[[nodiscard]] inline Status ok() {
  return Status{};
}

}  // namespace aegis
