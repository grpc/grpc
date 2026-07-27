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

#include "src/core/xds/grpc/streaming_call_promise_wrapper.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/util/sync.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// EventHandler bridges callback-driven events from XdsTransport::StreamingCall
// into StreamingCallPromiseWrapper. It holds a weak reference to avoid
// reference cycles between the transport stream and the wrapper.
class StreamingCallPromiseWrapper::EventHandler final
    : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
 public:
  explicit EventHandler(
      WeakRefCountedPtr<StreamingCallPromiseWrapper> promise_wrapper)
      : promise_wrapper_(std::move(promise_wrapper)) {}

  void OnRequestSent(bool ok) override { promise_wrapper_->OnRequestSent(ok); }

  void OnRecvMessage(absl::string_view payload) override {
    promise_wrapper_->OnRecvMessage(payload);
  }

  void OnStatusReceived(absl::Status status) override {
    promise_wrapper_->OnStatusReceived(std::move(status));
  }

 private:
  WeakRefCountedPtr<StreamingCallPromiseWrapper> promise_wrapper_;
};

StreamingCallPromiseWrapper::StreamingCallPromiseWrapper(
    XdsTransport& transport, const char* method, bool /*wait_for_ready*/) {
  auto internal_event_handler = std::make_unique<EventHandler>(
      WeakRefAsSubclass<StreamingCallPromiseWrapper>());
  call_ =
      transport.CreateStreamingCall(method, std::move(internal_event_handler));
}

Poll<StatusFlag> StreamingCallPromiseWrapper::PollSend() {
  MutexLock lock(&mu_);
  SendState state = send_state_.load();
  // If the send is still in flight on the transport, register the current
  // activity's waker to be notified when OnRequestSent is invoked.
  if (state == SendState::kSendMessageInFlight ||
      state == SendState::kSendMessageInFlightAndHalfCloseRequested) {
    send_message_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
    return Pending{};
  }
  // If the send failed or the stream terminated while the send was pending,
  // resolve to failure.
  if (state == SendState::kSendFailed) {
    return Failure{};
  }
  return Success{};
}

Poll<ValueOrFailure<std::optional<std::string>>>
StreamingCallPromiseWrapper::PollPullMessage() {
  bool start_recv = false;
  {
    MutexLock lock(&mu_);
    // Check if a message has already been received and buffered.
    if (incoming_message_.has_value()) {
      std::string msg = std::move(*incoming_message_);
      incoming_message_.reset();
      return ValueOrFailure<std::optional<std::string>>(
          std::optional<std::string>(std::move(msg)));
    }
    // Check if the stream has terminated.
    if (status_.has_value()) {
      if (!status_->ok()) return Failure{};
      return ValueOrFailure<std::optional<std::string>>(std::nullopt);
    }
    // If no read is currently active on the underlying stream, initiate one.
    if (!recv_message_in_flight_) {
      recv_message_in_flight_ = true;
      start_recv = true;
    }
  }
  // Initiate the read on the transport without holding mu_.
  if (start_recv) {
    call_->StartRecvMessage();
  }
  MutexLock lock(&mu_);
  // Suspend the promise and register the activity waker for notification.
  recv_message_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  return Pending{};
}

Poll<absl::Status>
StreamingCallPromiseWrapper::PollPullServerTrailingMetadata() {
  MutexLock lock(&mu_);
  // If the stream has terminated, resolve to the final status immediately.
  if (status_.has_value()) {
    return *status_;
  }
  // Suspend the promise until OnStatusReceived or Orphaned is invoked.
  status_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  return Pending{};
}

void StreamingCallPromiseWrapper::OnRequestSent(bool ok) {
  Waker waker;
  bool send_half_close = false;
  {
    MutexLock lock(&mu_);
    if (!ok) {
      send_state_.store(SendState::kSendFailed);
      waker = std::move(send_message_waker_);
    } else {
      SendState state = send_state_.load();
      if (state == SendState::kSendMessageInFlightAndHalfCloseRequested) {
        send_state_.store(SendState::kHalfCloseInFlight);
        send_half_close = true;
        waker = std::move(send_message_waker_);
      } else if (state == SendState::kSendMessageInFlight) {
        send_state_.store(SendState::kIdle);
        waker = std::move(send_message_waker_);
      } else if (state == SendState::kHalfCloseInFlight) {
        send_state_.store(SendState::kHalfClosed);
      }
    }
  }
  // Initiate half-close outside the lock if requested while send was pending.
  if (send_half_close && call_ != nullptr) {
    call_->SendHalfClose();
  }
  // Wake any waiting send promise outside the lock.
  waker.Wakeup();
}

void StreamingCallPromiseWrapper::OnRecvMessage(absl::string_view payload) {
  Waker waker;
  {
    MutexLock lock(&mu_);
    incoming_message_ = std::string(payload);
    recv_message_in_flight_ = false;
    waker = std::move(recv_message_waker_);
  }
  waker.Wakeup();
}

void StreamingCallPromiseWrapper::OnStatusReceived(absl::Status status) {
  Waker send_waker;
  Waker recv_waker;
  Waker status_waker;
  {
    MutexLock lock(&mu_);
    status_ = std::move(status);
    recv_message_in_flight_ = false;
    send_state_.store(SendState::kSendFailed);
    send_waker = std::move(send_message_waker_);
    recv_waker = std::move(recv_message_waker_);
    status_waker = std::move(status_waker_);
  }
  // Wake all pending promises so they can observe stream termination.
  send_waker.Wakeup();
  recv_waker.Wakeup();
  status_waker.Wakeup();
}

void StreamingCallPromiseWrapper::SendHalfClose() {
  SendState expected = SendState::kSendMessageInFlight;
  // If a send is in flight, record that half-close was requested. OnRequestSent
  // will issue the half-close once the message send completes.
  if (send_state_.compare_exchange_strong(
          expected, SendState::kSendMessageInFlightAndHalfCloseRequested)) {
    return;
  }
  expected = SendState::kIdle;
  if (send_state_.compare_exchange_strong(expected,
                                          SendState::kHalfCloseInFlight)) {
    call_->SendHalfClose();
  }
}

void StreamingCallPromiseWrapper::Orphaned() {
  Waker send_waker;
  Waker recv_waker;
  Waker status_waker;
  {
    MutexLock lock(&mu_);
    send_state_.store(SendState::kSendFailed);
    if (!status_.has_value()) {
      status_ = absl::CancelledError("Stream closed");
    }
    send_waker = std::move(send_message_waker_);
    recv_waker = std::move(recv_message_waker_);
    status_waker = std::move(status_waker_);
  }
  // Release the underlying streaming call.
  auto call = std::move(call_);
  call.reset();
  // Wake all pending promises so they observe cancellation immediately.
  send_waker.Wakeup();
  recv_waker.Wakeup();
  status_waker.Wakeup();
}

}  // namespace grpc_core
