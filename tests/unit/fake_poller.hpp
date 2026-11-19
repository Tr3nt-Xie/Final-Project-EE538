#pragma once

#include "aegisproxy/network/poller.hpp"

#include <deque>
#include <map>
#include <vector>

namespace aegis::net::testing {

/// Deterministic Poller for EventLoop unit tests: records registrations and returns
/// whatever events the test queues.
class FakePoller final : public Poller {
 public:
  struct Registration {
    EventMask interest;
    HandlerId token;
  };

  Status add(Fd fd, EventMask interest, HandlerId token) override {
    if (registered.contains(fd))
      return Error{ErrorCode::AlreadyExists, "fd already added"};
    registered[fd] = Registration{interest, token};
    return ok();
  }
  Status modify(Fd fd, EventMask interest, HandlerId token) override {
    auto it = registered.find(fd);
    if (it == registered.end())
      return Error{ErrorCode::NotFound, "fd not registered"};
    it->second = Registration{interest, token};
    ++modify_calls;
    return ok();
  }
  Status remove(Fd fd) override {
    if (registered.erase(fd) == 0)
      return Error{ErrorCode::NotFound, "fd not registered"};
    ++remove_calls;
    return ok();
  }
  Result<std::size_t> wait(std::span<PollEvent> out, std::chrono::milliseconds) override {
    ++wait_calls;
    std::size_t n = 0;
    while (n < out.size() && !pending.empty()) {
      out[n++] = pending.front();
      pending.pop_front();
    }
    return n;
  }

  void push(HandlerId token, EventMask events) { pending.push_back(PollEvent{token, events}); }

  std::map<Fd, Registration> registered;
  std::deque<PollEvent> pending;
  int modify_calls{0};
  int remove_calls{0};
  int wait_calls{0};
};

}  // namespace aegis::net::testing
