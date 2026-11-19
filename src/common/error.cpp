#include "aegisproxy/common/result.hpp"

#include <cstring>
#include <string>

namespace aegis {

Error Error::from_errno(std::string_view what, int err) {
  return Error{ErrorCode::SystemError, std::string(what), err};
}

Error Error::not_implemented(std::string_view feature) {
  return Error{ErrorCode::NotImplemented, std::string(feature) + " is not implemented yet"};
}

std::string Error::to_string() const {
  std::string out(aegis::to_string(code));
  if (!message.empty()) {
    out += ": ";
    out += message;
  }
  if (sys_errno != 0) {
    out += " (errno ";
    out += std::to_string(sys_errno);
    out += ": ";
    out += std::strerror(sys_errno);
    out += ")";
  }
  return out;
}

}  // namespace aegis
