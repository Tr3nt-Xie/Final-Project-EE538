#include "aegisproxy/network/connection_state_machine.hpp"

#include <gtest/gtest.h>

namespace aegis::net {

using S = ConnectionState;

TEST(ConnectionStateMachine, HappyPathThroughAllStates) {
  ConnectionStateMachine sm;
  EXPECT_EQ(sm.state(), S::Accepted);
  for (S next :
       {S::ReadingRequest, S::SelectingBackend, S::ConnectingBackend, S::ForwardingRequest,
        S::AwaitingResponse, S::WritingResponse, S::ReadingRequest, S::Closing, S::Closed}) {
    ASSERT_TRUE(sm.transition_to(next)) << to_string(sm.state()) << " -> " << to_string(next);
    EXPECT_EQ(sm.state(), next);
  }
  EXPECT_TRUE(sm.is_terminal());
}

TEST(ConnectionStateMachine, AnyLiveStateMayClose) {
  for (S from : {S::Accepted, S::ReadingRequest, S::SelectingBackend, S::ConnectingBackend,
                 S::ForwardingRequest, S::AwaitingResponse, S::WritingResponse}) {
    EXPECT_TRUE(ConnectionStateMachine::is_valid_transition(from, S::Closing)) << to_string(from);
  }
}

TEST(ConnectionStateMachine, RejectsSkippingAndBackwards) {
  ConnectionStateMachine sm;
  Status st = sm.transition_to(S::ForwardingRequest);
  ASSERT_FALSE(st);
  EXPECT_EQ(st.error().code, ErrorCode::InvalidState);
  EXPECT_EQ(sm.state(), S::Accepted) << "failed transition must not change state";
  EXPECT_FALSE(ConnectionStateMachine::is_valid_transition(S::AwaitingResponse, S::ReadingRequest));
  EXPECT_FALSE(ConnectionStateMachine::is_valid_transition(S::Closed, S::Closing));
  EXPECT_FALSE(ConnectionStateMachine::is_valid_transition(S::Closing, S::ReadingRequest));
  EXPECT_FALSE(ConnectionStateMachine::is_valid_transition(S::Accepted, S::Closed));
}

TEST(ConnectionStateMachine, ProxyGeneratedErrorsJumpToWritingResponse) {
  EXPECT_TRUE(ConnectionStateMachine::is_valid_transition(S::SelectingBackend, S::WritingResponse));
  EXPECT_TRUE(
      ConnectionStateMachine::is_valid_transition(S::ConnectingBackend, S::WritingResponse));
  EXPECT_TRUE(ConnectionStateMachine::is_valid_transition(S::ReadingRequest, S::WritingResponse));
}

}  // namespace aegis::net
