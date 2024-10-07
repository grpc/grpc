// Copyright 2024 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_CALL_STATE_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_CALL_STATE_H

#include <grpc/support/port_platform.h>

#include "absl/types/optional.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/util/crash.h"

namespace grpc_core {

class CallState {
 public:
  CallState();
  // Start the call: allows pulls to proceed
  void Start();
  // PUSH: client -> server
  void BeginPushClientToServerMessage();
  Poll<StatusFlag> PollPushClientToServerMessage();
  void ClientToServerHalfClose();
  // PULL: client -> server
  void BeginPullClientInitialMetadata();
  void FinishPullClientInitialMetadata();
  Poll<ValueOrFailure<bool>> PollPullClientToServerMessageAvailable();
  void FinishPullClientToServerMessage();
  // PUSH: server -> client
  StatusFlag PushServerInitialMetadata();
  void BeginPushServerToClientMessage();
  Poll<StatusFlag> PollPushServerToClientMessage();
  bool PushServerTrailingMetadata(bool cancel);
  // PULL: server -> client
  Poll<bool> PollPullServerInitialMetadataAvailable();
  void FinishPullServerInitialMetadata();
  Poll<ValueOrFailure<bool>> PollPullServerToClientMessageAvailable();
  void FinishPullServerToClientMessage();
  Poll<Empty> PollServerTrailingMetadataAvailable();
  void FinishPullServerTrailingMetadata();
  bool WasServerTrailingMetadataPulled() const;
  // Resolves after server trailing metadata has been pulled, to true if the
  // call was cancelled, and false otherwise.
  Poll<bool> PollWasCancelled();
  // Return true if server trailing metadata has been pushed *and* that push was
  // a cancellation.
  bool WasCancelledPushed() const;
  // Debug
  std::string DebugString() const;

  friend std::ostream& operator<<(std::ostream& out,
                                  const CallState& call_state) {
    return out << call_state.DebugString();
  }

 private:
  enum class ClientToServerPullState : uint16_t {
    // Ready to read: client initial metadata is there, but not yet processed
    kBegin,
    // Processing client initial metadata
    kProcessingClientInitialMetadata,
    // Main call loop: not reading
    kIdle,
    // Main call loop: reading but no message available
    kReading,
    // Main call loop: processing one message
    kProcessingClientToServerMessage,
    // Processing complete
    kTerminated,
  };
  static const char* ClientToServerPullStateString(
      ClientToServerPullState state) {
    switch (state) {
      case ClientToServerPullState::kBegin:
        return "Begin";
      case ClientToServerPullState::kProcessingClientInitialMetadata:
        return "ProcessingClientInitialMetadata";
      case ClientToServerPullState::kIdle:
        return "Idle";
      case ClientToServerPullState::kReading:
        return "Reading";
      case ClientToServerPullState::kProcessingClientToServerMessage:
        return "ProcessingClientToServerMessage";
      case ClientToServerPullState::kTerminated:
        return "Terminated";
    }
  }
  template <typename Sink>
  friend void AbslStringify(Sink& out, ClientToServerPullState state) {
    out.Append(ClientToServerPullStateString(state));
  }
  friend std::ostream& operator<<(std::ostream& out,
                                  ClientToServerPullState state) {
    return out << ClientToServerPullStateString(state);
  }
  enum class ClientToServerPushState : uint16_t {
    kIdle,
    kPushedMessage,
    kPushedHalfClose,
    kPushedMessageAndHalfClosed,
    kFinished,
  };
  static const char* ClientToServerPushStateString(
      ClientToServerPushState state) {
    switch (state) {
      case ClientToServerPushState::kIdle:
        return "Idle";
      case ClientToServerPushState::kPushedMessage:
        return "PushedMessage";
      case ClientToServerPushState::kPushedHalfClose:
        return "PushedHalfClose";
      case ClientToServerPushState::kPushedMessageAndHalfClosed:
        return "PushedMessageAndHalfClosed";
      case ClientToServerPushState::kFinished:
        return "Finished";
    }
  }
  template <typename Sink>
  friend void AbslStringify(Sink& out, ClientToServerPushState state) {
    out.Append(ClientToServerPushStateString(state));
  }
  friend std::ostream& operator<<(std::ostream& out,
                                  ClientToServerPushState state) {
    return out << ClientToServerPushStateString(state);
  }
  enum class ServerToClientPullState : uint16_t {
    // Not yet started: cannot read
    kUnstarted,
    kUnstartedReading,
    kStarted,
    kStartedReading,
    // Processing server initial metadata
    kProcessingServerInitialMetadata,
    kProcessingServerInitialMetadataReading,
    // Main call loop: not reading
    kIdle,
    // Main call loop: reading but no message available
    kReading,
    // Main call loop: processing one message
    kProcessingServerToClientMessage,
    kTerminated,
  };
  static const char* ServerToClientPullStateString(
      ServerToClientPullState state) {
    switch (state) {
      case ServerToClientPullState::kUnstarted:
        return "Unstarted";
      case ServerToClientPullState::kUnstartedReading:
        return "UnstartedReading";
      case ServerToClientPullState::kStarted:
        return "Started";
      case ServerToClientPullState::kStartedReading:
        return "StartedReading";
      case ServerToClientPullState::kProcessingServerInitialMetadata:
        return "ProcessingServerInitialMetadata";
      case ServerToClientPullState::kProcessingServerInitialMetadataReading:
        return "ProcessingServerInitialMetadataReading";
      case ServerToClientPullState::kIdle:
        return "Idle";
      case ServerToClientPullState::kReading:
        return "Reading";
      case ServerToClientPullState::kProcessingServerToClientMessage:
        return "ProcessingServerToClientMessage";
      case ServerToClientPullState::kTerminated:
        return "Terminated";
    }
  }
  template <typename Sink>
  friend void AbslStringify(Sink& out, ServerToClientPullState state) {
    out.Append(ServerToClientPullStateString(state));
  }
  friend std::ostream& operator<<(std::ostream& out,
                                  ServerToClientPullState state) {
    return out << ServerToClientPullStateString(state);
  }
  enum class ServerToClientPushState : uint16_t {
    kStart,
    kPushedServerInitialMetadata,
    kPushedServerInitialMetadataAndPushedMessage,
    kTrailersOnly,
    kIdle,
    kPushedMessage,
    kFinished,
  };
  static const char* ServerToClientPushStateString(
      ServerToClientPushState state) {
    switch (state) {
      case ServerToClientPushState::kStart:
        return "Start";
      case ServerToClientPushState::kPushedServerInitialMetadata:
        return "PushedServerInitialMetadata";
      case ServerToClientPushState::
          kPushedServerInitialMetadataAndPushedMessage:
        return "PushedServerInitialMetadataAndPushedMessage";
      case ServerToClientPushState::kTrailersOnly:
        return "TrailersOnly";
      case ServerToClientPushState::kIdle:
        return "Idle";
      case ServerToClientPushState::kPushedMessage:
        return "PushedMessage";
      case ServerToClientPushState::kFinished:
        return "Finished";
    }
  }
  template <typename Sink>
  friend void AbslStringify(Sink& out, ServerToClientPushState state) {
    out.Append(ServerToClientPushStateString(state));
  }
  friend std::ostream& operator<<(std::ostream& out,
                                  ServerToClientPushState state) {
    return out << ServerToClientPushStateString(state);
  }
  enum class ServerTrailingMetadataState : uint16_t {
    kNotPushed,
    kPushed,
    kPushedCancel,
    kPulled,
    kPulledCancel,
  };
  static const char* ServerTrailingMetadataStateString(
      ServerTrailingMetadataState state) {
    switch (state) {
      case ServerTrailingMetadataState::kNotPushed:
        return "NotPushed";
      case ServerTrailingMetadataState::kPushed:
        return "Pushed";
      case ServerTrailingMetadataState::kPushedCancel:
        return "PushedCancel";
      case ServerTrailingMetadataState::kPulled:
        return "Pulled";
      case ServerTrailingMetadataState::kPulledCancel:
        return "PulledCancel";
    }
  }
  template <typename Sink>
  friend void AbslStringify(Sink& out, ServerTrailingMetadataState state) {
    out.Append(ServerTrailingMetadataStateString(state));
  }
  friend std::ostream& operator<<(std::ostream& out,
                                  ServerTrailingMetadataState state) {
    return out << ServerTrailingMetadataStateString(state);
  }
  ClientToServerPullState client_to_server_pull_state_ : 3;
  ClientToServerPushState client_to_server_push_state_ : 3;
  ServerToClientPullState server_to_client_pull_state_ : 4;
  ServerToClientPushState server_to_client_push_state_ : 3;
  ServerTrailingMetadataState server_trailing_metadata_state_ : 3;
  IntraActivityWaiter client_to_server_pull_waiter_;
  IntraActivityWaiter server_to_client_pull_waiter_;
  IntraActivityWaiter client_to_server_push_waiter_;
  IntraActivityWaiter server_to_client_push_waiter_;
  IntraActivityWaiter server_trailing_metadata_waiter_;
};

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline CallState::CallState()
    : client_to_server_pull_state_(ClientToServerPullState::kBegin),
      client_to_server_push_state_(ClientToServerPushState::kIdle),
      server_to_client_pull_state_(ServerToClientPullState::kUnstarted),
      server_to_client_push_state_(ServerToClientPushState::kStart),
      server_trailing_metadata_state_(ServerTrailingMetadataState::kNotPushed) {
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void CallState::Start() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] Start: "
      << GRPC_DUMP_ARGS(this, server_to_client_pull_state_);
  switch (server_to_client_pull_state_) {
    case ServerToClientPullState::kUnstarted:
      server_to_client_pull_state_ = ServerToClientPullState::kStarted;
      server_to_client_pull_waiter_.Wake();
      break;
    case ServerToClientPullState::kUnstartedReading:
      server_to_client_pull_state_ = ServerToClientPullState::kStartedReading;
      server_to_client_pull_waiter_.Wake();
      break;
    case ServerToClientPullState::kStarted:
    case ServerToClientPullState::kStartedReading:
    case ServerToClientPullState::kProcessingServerInitialMetadata:
    case ServerToClientPullState::kProcessingServerInitialMetadataReading:
    case ServerToClientPullState::kIdle:
    case ServerToClientPullState::kReading:
    case ServerToClientPullState::kProcessingServerToClientMessage:
      LOG(FATAL) << "Start called twice";
    case ServerToClientPullState::kTerminated:
      break;
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void
CallState::BeginPushClientToServerMessage() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] BeginPushClientToServerMessage: "
      << GRPC_DUMP_ARGS(this, client_to_server_push_state_);
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
      client_to_server_push_state_ = ClientToServerPushState::kPushedMessage;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kPushedMessage:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      LOG(FATAL) << "PushClientToServerMessage called twice concurrently";
      break;
    case ClientToServerPushState::kPushedHalfClose:
      LOG(FATAL) << "PushClientToServerMessage called after half-close";
      break;
    case ClientToServerPushState::kFinished:
      break;
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline Poll<StatusFlag>
CallState::PollPushClientToServerMessage() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PollPushClientToServerMessage: "
      << GRPC_DUMP_ARGS(this, client_to_server_push_state_);
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
    case ClientToServerPushState::kPushedHalfClose:
      return Success{};
    case ClientToServerPushState::kPushedMessage:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      return client_to_server_push_waiter_.pending();
    case ClientToServerPushState::kFinished:
      return Failure{};
  }
  Crash("Unreachable");
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void
CallState::ClientToServerHalfClose() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] ClientToServerHalfClose: "
      << GRPC_DUMP_ARGS(this, client_to_server_push_state_);
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
      client_to_server_push_state_ = ClientToServerPushState::kPushedHalfClose;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kPushedMessage:
      client_to_server_push_state_ =
          ClientToServerPushState::kPushedMessageAndHalfClosed;
      break;
    case ClientToServerPushState::kPushedHalfClose:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      LOG(FATAL) << "ClientToServerHalfClose called twice";
      break;
    case ClientToServerPushState::kFinished:
      break;
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void
CallState::BeginPullClientInitialMetadata() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] BeginPullClientInitialMetadata: "
      << GRPC_DUMP_ARGS(this, client_to_server_pull_state_);
  switch (client_to_server_pull_state_) {
    case ClientToServerPullState::kBegin:
      client_to_server_pull_state_ =
          ClientToServerPullState::kProcessingClientInitialMetadata;
      break;
    case ClientToServerPullState::kProcessingClientInitialMetadata:
    case ClientToServerPullState::kIdle:
    case ClientToServerPullState::kReading:
    case ClientToServerPullState::kProcessingClientToServerMessage:
      LOG(FATAL) << "BeginPullClientInitialMetadata called twice";
      break;
    case ClientToServerPullState::kTerminated:
      break;
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void
CallState::FinishPullClientInitialMetadata() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] FinishPullClientInitialMetadata: "
      << GRPC_DUMP_ARGS(this, client_to_server_pull_state_);
  switch (client_to_server_pull_state_) {
    case ClientToServerPullState::kBegin:
      LOG(FATAL) << "FinishPullClientInitialMetadata called before Begin";
      break;
    case ClientToServerPullState::kProcessingClientInitialMetadata:
      client_to_server_pull_state_ = ClientToServerPullState::kIdle;
      client_to_server_pull_waiter_.Wake();
      break;
    case ClientToServerPullState::kIdle:
    case ClientToServerPullState::kReading:
    case ClientToServerPullState::kProcessingClientToServerMessage:
      LOG(FATAL) << "Out of order FinishPullClientInitialMetadata";
      break;
    case ClientToServerPullState::kTerminated:
      break;
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline Poll<ValueOrFailure<bool>>
CallState::PollPullClientToServerMessageAvailable() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PollPullClientToServerMessageAvailable: "
      << GRPC_DUMP_ARGS(this, client_to_server_pull_state_,
                        client_to_server_push_state_);
  switch (client_to_server_pull_state_) {
    case ClientToServerPullState::kBegin:
    case ClientToServerPullState::kProcessingClientInitialMetadata:
      return client_to_server_pull_waiter_.pending();
    case ClientToServerPullState::kIdle:
      client_to_server_pull_state_ = ClientToServerPullState::kReading;
      ABSL_FALLTHROUGH_INTENDED;
    case ClientToServerPullState::kReading:
      break;
    case ClientToServerPullState::kProcessingClientToServerMessage:
      LOG(FATAL) << "PollPullClientToServerMessageAvailable called while "
                    "processing a message";
      break;
    case ClientToServerPullState::kTerminated:
      return Failure{};
  }
  DCHECK_EQ(client_to_server_pull_state_, ClientToServerPullState::kReading);
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
      return client_to_server_push_waiter_.pending();
    case ClientToServerPushState::kPushedMessage:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      client_to_server_pull_state_ =
          ClientToServerPullState::kProcessingClientToServerMessage;
      return true;
    case ClientToServerPushState::kPushedHalfClose:
      return false;
    case ClientToServerPushState::kFinished:
      client_to_server_pull_state_ = ClientToServerPullState::kTerminated;
      return Failure{};
  }
  Crash("Unreachable");
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void
CallState::FinishPullClientToServerMessage() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] FinishPullClientToServerMessage: "
      << GRPC_DUMP_ARGS(this, client_to_server_pull_state_,
                        client_to_server_push_state_);
  switch (client_to_server_pull_state_) {
    case ClientToServerPullState::kBegin:
    case ClientToServerPullState::kProcessingClientInitialMetadata:
      LOG(FATAL) << "FinishPullClientToServerMessage called before Begin";
      break;
    case ClientToServerPullState::kIdle:
      LOG(FATAL) << "FinishPullClientToServerMessage called twice";
      break;
    case ClientToServerPullState::kReading:
      LOG(FATAL) << "FinishPullClientToServerMessage called before "
                    "PollPullClientToServerMessageAvailable";
      break;
    case ClientToServerPullState::kProcessingClientToServerMessage:
      client_to_server_pull_state_ = ClientToServerPullState::kIdle;
      client_to_server_pull_waiter_.Wake();
      break;
    case ClientToServerPullState::kTerminated:
      break;
  }
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kPushedMessage:
      client_to_server_push_state_ = ClientToServerPushState::kIdle;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kIdle:
    case ClientToServerPushState::kPushedHalfClose:
      LOG(FATAL) << "FinishPullClientToServerMessage called without a message";
      break;
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      client_to_server_push_state_ = ClientToServerPushState::kPushedHalfClose;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kFinished:
      break;
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline StatusFlag
CallState::PushServerInitialMetadata() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PushServerInitialMetadata: "
      << GRPC_DUMP_ARGS(this, server_to_client_push_state_,
                        server_trailing_metadata_state_);
  if (server_trailing_metadata_state_ !=
      ServerTrailingMetadataState::kNotPushed) {
    return Failure{};
  }
  CHECK_EQ(server_to_client_push_state_, ServerToClientPushState::kStart);
  server_to_client_push_state_ =
      ServerToClientPushState::kPushedServerInitialMetadata;
  server_to_client_push_waiter_.Wake();
  return Success{};
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void
CallState::BeginPushServerToClientMessage() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] BeginPushServerToClientMessage: "
      << GRPC_DUMP_ARGS(this, server_to_client_push_state_);
  switch (server_to_client_push_state_) {
    case ServerToClientPushState::kStart:
      LOG(FATAL) << "BeginPushServerToClientMessage called before "
                    "PushServerInitialMetadata";
      break;
    case ServerToClientPushState::kPushedServerInitialMetadata:
      server_to_client_push_state_ =
          ServerToClientPushState::kPushedServerInitialMetadataAndPushedMessage;
      break;
    case ServerToClientPushState::kPushedServerInitialMetadataAndPushedMessage:
    case ServerToClientPushState::kPushedMessage:
      LOG(FATAL) << "BeginPushServerToClientMessage called twice concurrently";
      break;
    case ServerToClientPushState::kTrailersOnly:
      // Will fail in poll.
      break;
    case ServerToClientPushState::kIdle:
      server_to_client_push_state_ = ServerToClientPushState::kPushedMessage;
      server_to_client_push_waiter_.Wake();
      break;
    case ServerToClientPushState::kFinished:
      break;
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline Poll<StatusFlag>
CallState::PollPushServerToClientMessage() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PollPushServerToClientMessage: "
      << GRPC_DUMP_ARGS(this, server_to_client_push_state_);
  switch (server_to_client_push_state_) {
    case ServerToClientPushState::kStart:
    case ServerToClientPushState::kPushedServerInitialMetadata:
      LOG(FATAL) << "PollPushServerToClientMessage called before "
                 << "PushServerInitialMetadata";
    case ServerToClientPushState::kTrailersOnly:
      return false;
    case ServerToClientPushState::kPushedMessage:
    case ServerToClientPushState::kPushedServerInitialMetadataAndPushedMessage:
      return server_to_client_push_waiter_.pending();
    case ServerToClientPushState::kIdle:
      return Success{};
    case ServerToClientPushState::kFinished:
      return Failure{};
  }
  Crash("Unreachable");
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline bool
CallState::PushServerTrailingMetadata(bool cancel) {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PushServerTrailingMetadata: "
      << GRPC_DUMP_ARGS(this, cancel, server_trailing_metadata_state_,
                        server_to_client_push_state_,
                        client_to_server_push_state_,
                        server_trailing_metadata_waiter_.DebugString());
  if (server_trailing_metadata_state_ !=
      ServerTrailingMetadataState::kNotPushed) {
    return false;
  }
  server_trailing_metadata_state_ =
      cancel ? ServerTrailingMetadataState::kPushedCancel
             : ServerTrailingMetadataState::kPushed;
  server_trailing_metadata_waiter_.Wake();
  switch (server_to_client_push_state_) {
    case ServerToClientPushState::kStart:
      server_to_client_push_state_ = ServerToClientPushState::kTrailersOnly;
      server_to_client_push_waiter_.Wake();
      break;
    case ServerToClientPushState::kPushedServerInitialMetadata:
    case ServerToClientPushState::kPushedServerInitialMetadataAndPushedMessage:
    case ServerToClientPushState::kPushedMessage:
      if (cancel) {
        server_to_client_push_state_ = ServerToClientPushState::kFinished;
        server_to_client_push_waiter_.Wake();
      }
      break;
    case ServerToClientPushState::kIdle:
      if (cancel) {
        server_to_client_push_state_ = ServerToClientPushState::kFinished;
        server_to_client_push_waiter_.Wake();
      }
      break;
    case ServerToClientPushState::kFinished:
    case ServerToClientPushState::kTrailersOnly:
      break;
  }
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
      client_to_server_push_state_ = ClientToServerPushState::kFinished;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kPushedMessage:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      client_to_server_push_state_ = ClientToServerPushState::kFinished;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kPushedHalfClose:
    case ClientToServerPushState::kFinished:
      break;
  }
  return true;
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline Poll<bool>
CallState::PollPullServerInitialMetadataAvailable() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PollPullServerInitialMetadataAvailable: "
      << GRPC_DUMP_ARGS(this, server_to_client_pull_state_,
                        server_to_client_push_state_);
  bool reading;
  switch (server_to_client_pull_state_) {
    case ServerToClientPullState::kUnstarted:
    case ServerToClientPullState::kUnstartedReading:
      if (server_to_client_push_state_ ==
          ServerToClientPushState::kTrailersOnly) {
        server_to_client_pull_state_ = ServerToClientPullState::kTerminated;
        return false;
      }
      server_to_client_push_waiter_.pending();
      return server_to_client_pull_waiter_.pending();
    case ServerToClientPullState::kStartedReading:
      reading = true;
      break;
    case ServerToClientPullState::kStarted:
      reading = false;
      break;
    case ServerToClientPullState::kProcessingServerInitialMetadata:
    case ServerToClientPullState::kProcessingServerInitialMetadataReading:
    case ServerToClientPullState::kIdle:
    case ServerToClientPullState::kReading:
    case ServerToClientPullState::kProcessingServerToClientMessage:
      LOG(FATAL) << "PollPullServerInitialMetadataAvailable called twice";
    case ServerToClientPullState::kTerminated:
      return false;
  }
  DCHECK(server_to_client_pull_state_ == ServerToClientPullState::kStarted ||
         server_to_client_pull_state_ ==
             ServerToClientPullState::kStartedReading)
      << server_to_client_pull_state_;
  switch (server_to_client_push_state_) {
    case ServerToClientPushState::kStart:
      return server_to_client_push_waiter_.pending();
    case ServerToClientPushState::kPushedServerInitialMetadata:
    case ServerToClientPushState::kPushedServerInitialMetadataAndPushedMessage:
      server_to_client_pull_state_ =
          reading
              ? ServerToClientPullState::kProcessingServerInitialMetadataReading
              : ServerToClientPullState::kProcessingServerInitialMetadata;
      server_to_client_pull_waiter_.Wake();
      return true;
    case ServerToClientPushState::kIdle:
    case ServerToClientPushState::kPushedMessage:
      LOG(FATAL)
          << "PollPullServerInitialMetadataAvailable after metadata processed";
    case ServerToClientPushState::kFinished:
      server_to_client_pull_state_ = ServerToClientPullState::kTerminated;
      server_to_client_pull_waiter_.Wake();
      return false;
    case ServerToClientPushState::kTrailersOnly:
      return false;
  }
  Crash("Unreachable");
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void
CallState::FinishPullServerInitialMetadata() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] FinishPullServerInitialMetadata: "
      << GRPC_DUMP_ARGS(this, server_to_client_pull_state_);
  switch (server_to_client_pull_state_) {
    case ServerToClientPullState::kUnstarted:
    case ServerToClientPullState::kUnstartedReading:
      LOG(FATAL) << "FinishPullServerInitialMetadata called before Start";
    case ServerToClientPullState::kStarted:
    case ServerToClientPullState::kStartedReading:
      CHECK_EQ(server_to_client_push_state_,
               ServerToClientPushState::kTrailersOnly);
      return;
    case ServerToClientPullState::kProcessingServerInitialMetadata:
      server_to_client_pull_state_ = ServerToClientPullState::kIdle;
      server_to_client_pull_waiter_.Wake();
      break;
    case ServerToClientPullState::kProcessingServerInitialMetadataReading:
      server_to_client_pull_state_ = ServerToClientPullState::kReading;
      server_to_client_pull_waiter_.Wake();
      break;
    case ServerToClientPullState::kIdle:
    case ServerToClientPullState::kReading:
    case ServerToClientPullState::kProcessingServerToClientMessage:
      LOG(FATAL) << "Out of order FinishPullServerInitialMetadata";
    case ServerToClientPullState::kTerminated:
      return;
  }
  DCHECK(server_to_client_pull_state_ == ServerToClientPullState::kIdle ||
         server_to_client_pull_state_ == ServerToClientPullState::kReading)
      << server_to_client_pull_state_;
  switch (server_to_client_push_state_) {
    case ServerToClientPushState::kStart:
      LOG(FATAL) << "FinishPullServerInitialMetadata called before initial "
                    "metadata consumed";
    case ServerToClientPushState::kPushedServerInitialMetadata:
      server_to_client_push_state_ = ServerToClientPushState::kIdle;
      server_to_client_push_waiter_.Wake();
      break;
    case ServerToClientPushState::kPushedServerInitialMetadataAndPushedMessage:
      server_to_client_push_state_ = ServerToClientPushState::kPushedMessage;
      server_to_client_push_waiter_.Wake();
      break;
    case ServerToClientPushState::kIdle:
    case ServerToClientPushState::kPushedMessage:
    case ServerToClientPushState::kTrailersOnly:
    case ServerToClientPushState::kFinished:
      LOG(FATAL) << "FinishPullServerInitialMetadata called twice";
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline Poll<ValueOrFailure<bool>>
CallState::PollPullServerToClientMessageAvailable() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PollPullServerToClientMessageAvailable: "
      << GRPC_DUMP_ARGS(this, server_to_client_pull_state_,
                        server_to_client_push_state_,
                        server_trailing_metadata_state_);
  switch (server_to_client_pull_state_) {
    case ServerToClientPullState::kUnstarted:
      server_to_client_pull_state_ = ServerToClientPullState::kUnstartedReading;
      return server_to_client_pull_waiter_.pending();
    case ServerToClientPullState::kProcessingServerInitialMetadata:
      server_to_client_pull_state_ =
          ServerToClientPullState::kProcessingServerInitialMetadataReading;
      return server_to_client_pull_waiter_.pending();
    case ServerToClientPullState::kUnstartedReading:
    case ServerToClientPullState::kProcessingServerInitialMetadataReading:
      return server_to_client_pull_waiter_.pending();
    case ServerToClientPullState::kStarted:
      server_to_client_pull_state_ = ServerToClientPullState::kStartedReading;
      ABSL_FALLTHROUGH_INTENDED;
    case ServerToClientPullState::kStartedReading:
      if (server_to_client_push_state_ ==
          ServerToClientPushState::kTrailersOnly) {
        return false;
      }
      return server_to_client_pull_waiter_.pending();
    case ServerToClientPullState::kIdle:
      server_to_client_pull_state_ = ServerToClientPullState::kReading;
      ABSL_FALLTHROUGH_INTENDED;
    case ServerToClientPullState::kReading:
      break;
    case ServerToClientPullState::kProcessingServerToClientMessage:
      LOG(FATAL) << "PollPullServerToClientMessageAvailable called while "
                    "processing a message";
    case ServerToClientPullState::kTerminated:
      return Failure{};
  }
  DCHECK_EQ(server_to_client_pull_state_, ServerToClientPullState::kReading);
  switch (server_to_client_push_state_) {
    case ServerToClientPushState::kStart:
    case ServerToClientPushState::kPushedServerInitialMetadata:
    case ServerToClientPushState::kPushedServerInitialMetadataAndPushedMessage:
      return server_to_client_push_waiter_.pending();
    case ServerToClientPushState::kIdle:
      if (server_trailing_metadata_state_ !=
          ServerTrailingMetadataState::kNotPushed) {
        return false;
      }
      server_trailing_metadata_waiter_.pending();
      return server_to_client_push_waiter_.pending();
    case ServerToClientPushState::kTrailersOnly:
      DCHECK_NE(server_trailing_metadata_state_,
                ServerTrailingMetadataState::kNotPushed);
      return false;
    case ServerToClientPushState::kPushedMessage:
      server_to_client_pull_state_ =
          ServerToClientPullState::kProcessingServerToClientMessage;
      server_to_client_pull_waiter_.Wake();
      return true;
    case ServerToClientPushState::kFinished:
      server_to_client_pull_state_ = ServerToClientPullState::kTerminated;
      server_to_client_pull_waiter_.Wake();
      return Failure{};
  }
  Crash("Unreachable");
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline void
CallState::FinishPullServerToClientMessage() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] FinishPullServerToClientMessage: "
      << GRPC_DUMP_ARGS(this, server_to_client_pull_state_,
                        server_to_client_push_state_);
  switch (server_to_client_pull_state_) {
    case ServerToClientPullState::kUnstarted:
    case ServerToClientPullState::kUnstartedReading:
    case ServerToClientPullState::kStarted:
    case ServerToClientPullState::kStartedReading:
    case ServerToClientPullState::kProcessingServerInitialMetadata:
    case ServerToClientPullState::kProcessingServerInitialMetadataReading:
      LOG(FATAL)
          << "FinishPullServerToClientMessage called before metadata available";
    case ServerToClientPullState::kIdle:
      LOG(FATAL) << "FinishPullServerToClientMessage called twice";
    case ServerToClientPullState::kReading:
      LOG(FATAL) << "FinishPullServerToClientMessage called before "
                 << "PollPullServerToClientMessageAvailable";
    case ServerToClientPullState::kProcessingServerToClientMessage:
      server_to_client_pull_state_ = ServerToClientPullState::kIdle;
      server_to_client_pull_waiter_.Wake();
      break;
    case ServerToClientPullState::kTerminated:
      break;
  }
  switch (server_to_client_push_state_) {
    case ServerToClientPushState::kPushedServerInitialMetadataAndPushedMessage:
    case ServerToClientPushState::kPushedServerInitialMetadata:
    case ServerToClientPushState::kStart:
      LOG(FATAL) << "FinishPullServerToClientMessage called before initial "
                    "metadata consumed";
    case ServerToClientPushState::kTrailersOnly:
      LOG(FATAL) << "FinishPullServerToClientMessage called after "
                    "PushServerTrailingMetadata";
    case ServerToClientPushState::kPushedMessage:
      server_to_client_push_state_ = ServerToClientPushState::kIdle;
      server_to_client_push_waiter_.Wake();
      break;
    case ServerToClientPushState::kIdle:
      LOG(FATAL) << "FinishPullServerToClientMessage called without a message";
    case ServerToClientPushState::kFinished:
      break;
  }
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline Poll<Empty>
CallState::PollServerTrailingMetadataAvailable() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PollServerTrailingMetadataAvailable: "
      << GRPC_DUMP_ARGS(this, server_to_client_pull_state_,
                        server_to_client_push_state_,
                        server_trailing_metadata_state_,
                        server_trailing_metadata_waiter_.DebugString());
  switch (server_to_client_pull_state_) {
    case ServerToClientPullState::kProcessingServerInitialMetadata:
    case ServerToClientPullState::kProcessingServerToClientMessage:
    case ServerToClientPullState::kProcessingServerInitialMetadataReading:
    case ServerToClientPullState::kUnstartedReading:
      return server_to_client_pull_waiter_.pending();
    case ServerToClientPullState::kStartedReading:
    case ServerToClientPullState::kReading:
      switch (server_to_client_push_state_) {
        case ServerToClientPushState::kTrailersOnly:
        case ServerToClientPushState::kIdle:
        case ServerToClientPushState::kStart:
        case ServerToClientPushState::kFinished:
          if (server_trailing_metadata_state_ !=
              ServerTrailingMetadataState::kNotPushed) {
            break;  // Ready for processing
          }
          ABSL_FALLTHROUGH_INTENDED;
        case ServerToClientPushState::kPushedServerInitialMetadata:
        case ServerToClientPushState::
            kPushedServerInitialMetadataAndPushedMessage:
        case ServerToClientPushState::kPushedMessage:
          server_to_client_push_waiter_.pending();
          return server_to_client_pull_waiter_.pending();
      }
      break;
    case ServerToClientPullState::kStarted:
    case ServerToClientPullState::kUnstarted:
    case ServerToClientPullState::kIdle:
      if (server_trailing_metadata_state_ !=
          ServerTrailingMetadataState::kNotPushed) {
        break;  // Ready for processing
      }
      return server_trailing_metadata_waiter_.pending();
    case ServerToClientPullState::kTerminated:
      break;
  }
  server_to_client_pull_state_ = ServerToClientPullState::kTerminated;
  server_to_client_pull_waiter_.Wake();
  switch (server_trailing_metadata_state_) {
    case ServerTrailingMetadataState::kNotPushed:
      LOG(FATAL) << "FinishPullServerTrailingMetadata called before "
                    "PollServerTrailingMetadataAvailable";
    case ServerTrailingMetadataState::kPushed:
      server_trailing_metadata_state_ = ServerTrailingMetadataState::kPulled;
      server_trailing_metadata_waiter_.Wake();
      break;
    case ServerTrailingMetadataState::kPushedCancel:
      server_trailing_metadata_state_ =
          ServerTrailingMetadataState::kPulledCancel;
      server_trailing_metadata_waiter_.Wake();
      break;
    case ServerTrailingMetadataState::kPulled:
    case ServerTrailingMetadataState::kPulledCancel:
      LOG(FATAL) << "FinishPullServerTrailingMetadata called twice";
  }
  return Empty{};
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline bool
CallState::WasServerTrailingMetadataPulled() const {
  switch (server_trailing_metadata_state_) {
    case ServerTrailingMetadataState::kNotPushed:
    case ServerTrailingMetadataState::kPushed:
    case ServerTrailingMetadataState::kPushedCancel:
      return false;
    case ServerTrailingMetadataState::kPulled:
    case ServerTrailingMetadataState::kPulledCancel:
      return true;
  }
  GPR_UNREACHABLE_CODE(Crash("unreachable"));
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline Poll<bool>
CallState::PollWasCancelled() {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PollWasCancelled: "
      << GRPC_DUMP_ARGS(this, server_trailing_metadata_state_);
  switch (server_trailing_metadata_state_) {
    case ServerTrailingMetadataState::kNotPushed:
    case ServerTrailingMetadataState::kPushed:
    case ServerTrailingMetadataState::kPushedCancel: {
      return server_trailing_metadata_waiter_.pending();
    }
    case ServerTrailingMetadataState::kPulled:
      return false;
    case ServerTrailingMetadataState::kPulledCancel:
      return true;
  }
  Crash("Unreachable");
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline bool CallState::WasCancelledPushed()
    const {
  GRPC_TRACE_LOG(call_state, INFO)
      << "[call_state] PollWasCancelledPushed: "
      << GRPC_DUMP_ARGS(this, server_trailing_metadata_state_);
  switch (server_trailing_metadata_state_) {
    case ServerTrailingMetadataState::kNotPushed:
    case ServerTrailingMetadataState::kPulled:
    case ServerTrailingMetadataState::kPushed:
      return false;
    case ServerTrailingMetadataState::kPushedCancel:
    case ServerTrailingMetadataState::kPulledCancel:
      return true;
  }
  Crash("Unreachable");
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_STATE_H
