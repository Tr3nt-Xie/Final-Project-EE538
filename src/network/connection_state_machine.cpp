#include "aegisproxy/network/connection_state_machine.hpp"

#include <string>

namespace aegis::net {

std::string_view to_string(ConnectionState state) noexcept {
  switch (state) {
    case ConnectionState::Accepted:
      return "Accepted";
    case ConnectionState::ReadingRequest:
      return "ReadingRequest";
    case ConnectionState::SelectingBackend:
      return "SelectingBackend";
    case ConnectionState::ConnectingBackend:
      return "ConnectingBackend";
    case ConnectionState::ForwardingRequest:
      return "ForwardingRequest";
    case ConnectionState::AwaitingResponse:
      return "AwaitingResponse";
    case ConnectionState::WritingResponse:
      return "WritingResponse";
    case ConnectionState::Closing:
      return "Closing";
    case ConnectionState::Closed:
      return "Closed";
  }
  return "?";
}

bool ConnectionStateMachine::is_valid_transition(ConnectionState from,
                                                 ConnectionState to) noexcept {
  using S = ConnectionState;
  if (from == S::Closed)
    return false;
  if (to == S::Closing)
    return from != S::Closing;
  if (to == S::Closed)
    return from == S::Closing;
  switch (from) {
    case S::Accepted:
      return to == S::ReadingRequest;
    case S::ReadingRequest:
      return to == S::SelectingBackend || to == S::WritingResponse;
    case S::SelectingBackend:
      return to == S::ConnectingBackend || to == S::WritingResponse;
    case S::ConnectingBackend:
      return to == S::ForwardingRequest || to == S::WritingResponse;
    case S::ForwardingRequest:
      return to == S::AwaitingResponse || to == S::WritingResponse;
    case S::AwaitingResponse:
      return to == S::WritingResponse;
    case S::WritingResponse:
      return to == S::ReadingRequest;
    case S::Closing:
    case S::Closed:
      return false;
  }
  return false;
}

Status ConnectionStateMachine::transition_to(ConnectionState next) {
  if (!is_valid_transition(state_, next)) {
    return Error{ErrorCode::InvalidState, "invalid connection transition " +
                                              std::string(to_string(state_)) + " -> " +
                                              std::string(to_string(next))};
  }
  state_ = next;
  return ok();
}

}  // namespace aegis::net
