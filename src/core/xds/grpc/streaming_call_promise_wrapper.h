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
#include "src/core/util/sync.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/base/thread_annotations.h"
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
class StreamingCallPromiseWrapper final
    : public DualRefCounted<StreamingCallPromiseWrapper> {
 public:
  using XdsTransport = XdsTransportFactory::XdsTransport;

  // Constructs a new streaming call wrapper for the given method on the
  // transport.
  explicit StreamingCallPromiseWrapper(XdsTransport& transport,
                                       const char* method,
                                       bool wait_for_ready = true);

  // Sends a message on the stream.
  //
  // Returns a promise that resolves to:
  // - Success{} when the message has been successfully transmitted.
  // - Failure{} if the stream has failed or closed.
  //
  // Contract: The caller MUST NOT call Send() again until the promise from the
  // previous Send() resolves or if a previous send failed or closed.
  auto Send(std::string msg) {
    SendState expected = SendState::kIdle;
    if (send_state_.compare_exchange_strong(expected,
                                            SendState::kSendMessageInFlight)) {
      if (call_ != nullptr) {
        call_->SendMessage(std::move(msg));
      }
    }
    return [self = WeakRefAsSubclass<StreamingCallPromiseWrapper>()]() {
      return self->PollSend();
    };
  }

  // Pulls an incoming message from the stream.
  //
  // Returns a promise that resolves to:
  // - ValueOrFailure(std::optional<string>(msg)) when a message is received.
  // - ValueOrFailure(std::nullopt) when the stream closes cleanly (EOF).
  // - Failure{} when the stream terminates with an error status.
  auto PullMessage() {
    return [self = WeakRefAsSubclass<StreamingCallPromiseWrapper>()]() {
      return self->PollPullMessage();
    };
  }

  // Waits for server trailing metadata and stream termination.
  //
  // Returns a promise that suspends until the server closes the stream,
  // resolving to the final absl::Status received from the server (or a
  // cancellation error if orphaned).
  auto PullServerTrailingMetadata() {
    return [self = WeakRefAsSubclass<StreamingCallPromiseWrapper>()]() {
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
    // Upon calling Send(), transitions to kSendMessageInFlight.
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
    // When complete, transitions to kHalfClosed.
    kHalfCloseInFlight,
    // The stream has been half-closed.
    // Once entering this state, we never leave.
    kHalfClosed,
  };

  // Internal polling implementation for Send().
  Poll<StatusFlag> PollSend();

  // Internal polling implementation for PullMessage().
  Poll<ValueOrFailure<std::optional<std::string>>> PollPullMessage();

  // Internal polling implementation for PullServerTrailingMetadata().
  Poll<absl::Status> PollPullServerTrailingMetadata();

  // Transport callback handlers invoked by EventHandler on the transport
  // thread.
  void OnRequestSent(bool ok);
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall> call_;
  std::atomic<SendState> send_state_{SendState::kIdle};

  mutable Mutex mu_;

  // Wakers for suspended promises.
  Waker send_message_waker_ ABSL_GUARDED_BY(mu_);
  Waker recv_message_waker_ ABSL_GUARDED_BY(mu_);
  Waker status_waker_ ABSL_GUARDED_BY(mu_);

  // State for incoming messages (PullMessage).
  bool recv_message_in_flight_ ABSL_GUARDED_BY(mu_) = false;
  std::optional<std::string> incoming_message_ ABSL_GUARDED_BY(mu_);

  // Trailing metadata status from the server (PullServerTrailingMetadata).
  std::optional<absl::Status> status_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_STREAMING_CALL_PROMISE_WRAPPER_H
