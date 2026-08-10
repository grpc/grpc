//
// Copyright 2026 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_XDS_GRPC_STREAMING_CALL_PROMISE_WRAPPER_H
#define GRPC_SRC_CORE_XDS_GRPC_STREAMING_CALL_PROMISE_WRAPPER_H

#include <atomic>
#include <optional>
#include <string>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// A promise-based wrapper around
// XdsTransportFactory::XdsTransport::StreamingCall.
//
// This class adapts callback-based xDS streaming transport calls into gRPC
// Core's promise-based architecture. It provides asynchronous promise
// primitives for sending requests, pulling incoming server messages, and
// waiting for server trailing metadata.
//
// DualRefCounted is used to manage object lifetime: strong references are held
// by clients and active promise workflows, while weak references are passed to
// the underlying transport event handler to avoid reference cycles.
class XdsStreamingCallPromiseWrapper final
    : public DualRefCounted<XdsStreamingCallPromiseWrapper> {
 public:
  using XdsTransport = XdsTransportFactory::XdsTransport;

  // Constructs a new streaming call wrapper for the given method on the
  // transport.
  XdsStreamingCallPromiseWrapper(XdsTransport& transport, const char* method);

  // Pushes a message on the stream.
  //
  // Returns a promise that resolves to StatusFlag, indicating whether the
  // message was sent successfully.
  //
  // Contract: The caller MUST NOT call PushMessage() again until the promise
  // from the previous PushMessage() resolves.
  auto PushMessage(std::string msg) {
    SendState expected = SendState::kIdle;
    GRPC_CHECK(send_state_.compare_exchange_strong(
        expected, SendState::kSendMessageInFlight));
    send_message_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
    call_->SendMessage(std::move(msg));
    return [self = WeakRefAsSubclass<XdsStreamingCallPromiseWrapper>()]() {
      return self->PollPushMessage();
    };
  }

  // Pulls an incoming message from the stream.
  //
  // Returns a promise that resolves to std::optional<std::string>.
  // The value will be nullopt when the stream is closed without
  // receiving a message.
  auto PullMessage() {
    RecvState recv_state = RecvState::kIdle;
    if (!recv_state_.compare_exchange_strong(recv_state,
                                             RecvState::kRecvMessageInFlight)) {
      GRPC_CHECK(recv_state == RecvState::kReceivedStatus);
      // Must be kReceivedStatus. Don't actually need to start the
      // recv_message op; we'll return nullopt on the first poll.
    } else {
      recv_message_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      call_->StartRecvMessage();
    }
    return [self = WeakRefAsSubclass<XdsStreamingCallPromiseWrapper>()]() {
      return self->PollPullMessage();
    };
  }

  // Waits for server trailing metadata and stream termination.
  //
  // Returns a promise that resolves to absl::Status, indicating
  // the final status of the call.
  auto PullServerTrailingMetadata() {
    recv_status_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
    return [self = WeakRefAsSubclass<XdsStreamingCallPromiseWrapper>()]() {
      return self->PollPullServerTrailingMetadata();
    };
  }

  // Requests a half-close (client EOF) on the stream, indicating that no
  // further messages will be sent by the client.
  void SendHalfClose();

 private:
  class EventHandler;

  // Called when all strong references are dropped.
  // Cancels the underlying stream and wakes any pending promises with a
  // cancellation status.
  void Orphaned() override;

  enum class SendState {
    // Initial state: no send op in flight.
    // Upon calling PushMessage(), transitions to kSendMessageInFlight.
    // Upon calling SendHalfClose(), transitions to kHalfCloseInFlight.
    kIdle,
    // Send message in flight.
    // If the send fails, transitions to kSendFailed.
    // Otherwise, if SendHalfClose() was called before the send completed,
    // transitions to kSendMessageInFlightAndHalfCloseRequested.
    // Otherwise, transitions to kIdle.
    kSendMessageInFlight,
    // A send failed.
    // Once entering this state, we never leave.
    kSendFailed,
    // A send message is in flight, and a half close has been requested.
    // If the send fails, transitions to kSendFailed.
    // Otherwise, transitions to kHalfCloseInFlight.
    kSendMessageInFlightAndHalfCloseRequested,
    // A half-close is in flight.
    kHalfCloseInFlight,
  };

  enum class RecvState {
    // Initial state.
    // If status is received, transitions to kReceivedStatus.
    // If PullMessage() is called, transitions to kRecvMessageInFlight.
    kIdle,
    // A PullMessage() promise is pending.
    // If a message is received, transitions to kIdle.
    // If status is received, transitions to kReceivedStatus.
    kRecvMessageInFlight,
    // Status has been received.
    // We never leave this state.
    kReceivedStatus,
  };

  // Internal polling implementation for PushMessage().
  Poll<StatusFlag> PollPushMessage();

  // Internal polling implementation for PullMessage().
  Poll<std::optional<std::string>> PollPullMessage();

  // Internal polling implementation for PullServerTrailingMetadata().
  Poll<absl::Status> PollPullServerTrailingMetadata();

  // Transport callback handlers invoked by EventHandler on the transport
  // thread.
  void OnRequestSent(bool ok);
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall> call_;

  // State for outgoing messages (PushMessage).
  std::atomic<SendState> send_state_{SendState::kIdle};
  Waker send_message_waker_;

  // State for incoming messages (PullMessage).
  std::atomic<RecvState> recv_state_{RecvState::kIdle};
  Waker recv_message_waker_;
  std::optional<std::string> recv_message_;

  // Trailing metadata status from the server (PullServerTrailingMetadata).
  Waker recv_status_waker_;
  absl::Status status_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_STREAMING_CALL_PROMISE_WRAPPER_H
