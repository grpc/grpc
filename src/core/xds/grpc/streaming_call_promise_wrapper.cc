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
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// EventHandler bridges callback-driven events from XdsTransport::StreamingCall
// into XdsStreamingCallPromiseWrapper. It holds a weak reference to avoid
// reference cycles between the transport stream and the wrapper.
class XdsStreamingCallPromiseWrapper::EventHandler final
    : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
 public:
  explicit EventHandler(
      WeakRefCountedPtr<XdsStreamingCallPromiseWrapper> promise_wrapper)
      : promise_wrapper_(std::move(promise_wrapper)) {}

  void OnRequestSent(bool ok) override { promise_wrapper_->OnRequestSent(ok); }

  void OnRecvMessage(absl::string_view payload) override {
    promise_wrapper_->OnRecvMessage(payload);
  }

  void OnStatusReceived(absl::Status status) override {
    promise_wrapper_->OnStatusReceived(std::move(status));
  }

 private:
  WeakRefCountedPtr<XdsStreamingCallPromiseWrapper> promise_wrapper_;
};

XdsStreamingCallPromiseWrapper::XdsStreamingCallPromiseWrapper(
    XdsTransport& transport, const char* method) {
  auto internal_event_handler = std::make_unique<EventHandler>(
      WeakRefAsSubclass<XdsStreamingCallPromiseWrapper>());
  call_ =
      transport.CreateStreamingCall(method, std::move(internal_event_handler));
}

Poll<StatusFlag> XdsStreamingCallPromiseWrapper::PollPushMessage() {
  MutexLock lock(&mu_);
  // If the send is still in flight on the transport, wait for completion.
  if (send_state_ == SendState::kSendMessageInFlight ||
      send_state_ == SendState::kSendMessageInFlightAndHalfCloseRequested) {
    return Pending{};
  }
  // If the send failed or the stream terminated while the send was pending,
  // resolve to failure.
  if (send_state_ == SendState::kSendFailed) {
    return Failure{};
  }
  return Success{};
}

Poll<std::optional<std::string>>
XdsStreamingCallPromiseWrapper::PollPullMessage() {
  MutexLock lock(&mu_);
  switch (recv_state_) {
    case RecvState::kIdle:
      return std::exchange(recv_message_, std::nullopt);
    case RecvState::kRecvMessageInFlight:
      return Pending{};
    case RecvState::kReceivedStatus:
      return std::nullopt;
  }
}

Poll<absl::Status>
XdsStreamingCallPromiseWrapper::PollPullServerTrailingMetadata() {
  MutexLock lock(&mu_);
  if (recv_state_ != RecvState::kReceivedStatus) return Pending{};
  return std::move(status_);
}

void XdsStreamingCallPromiseWrapper::OnRequestSent(bool ok) {
  Waker waker;
  bool send_half_close = false;
  {
    MutexLock lock(&mu_);
    if (!ok) {
      send_state_ = SendState::kSendFailed;
    } else {
      if (send_state_ == SendState::kSendMessageInFlightAndHalfCloseRequested) {
        send_state_ = SendState::kHalfCloseInFlight;
        send_half_close = true;
      } else if (send_state_ == SendState::kSendMessageInFlight) {
        send_state_ = SendState::kIdle;
      }
    }
    waker = std::move(send_message_waker_);
  }
  if (send_half_close) {
    call_->SendHalfClose();
  }
  // Wake any waiting send promise.
  waker.Wakeup();
}

void XdsStreamingCallPromiseWrapper::OnRecvMessage(absl::string_view payload) {
  Waker waker;
  {
    MutexLock lock(&mu_);
    recv_message_ = std::string(payload);
    if (recv_state_ == RecvState::kRecvMessageInFlight) {
      recv_state_ = RecvState::kIdle;
    }
    waker = std::move(recv_message_waker_);
  }
  waker.Wakeup();
}

void XdsStreamingCallPromiseWrapper::OnStatusReceived(absl::Status status) {
  Waker recv_message_waker;
  Waker recv_status_waker;
  {
    MutexLock lock(&mu_);
    status_ = std::move(status);
    if (recv_state_ == RecvState::kRecvMessageInFlight) {
      recv_message_waker = std::move(recv_message_waker_);
    }
    recv_state_ = RecvState::kReceivedStatus;
    recv_status_waker = std::move(recv_status_waker_);
  }
  recv_message_waker.Wakeup();
  recv_status_waker.Wakeup();
}

void XdsStreamingCallPromiseWrapper::SendHalfClose() {
  bool send_half_close = false;
  {
    MutexLock lock(&mu_);
    // If a send is in flight, record that half-close was requested.
    // OnRequestSent will issue the half-close once the message send completes.
    if (send_state_ == SendState::kSendMessageInFlight) {
      send_state_ = SendState::kSendMessageInFlightAndHalfCloseRequested;
    } else if (send_state_ == SendState::kIdle) {
      send_state_ = SendState::kHalfCloseInFlight;
      send_half_close = true;
    }
  }
  if (send_half_close) {
    call_->SendHalfClose();
  }
}

void XdsStreamingCallPromiseWrapper::Orphaned() {
  // Release the underlying streaming call.
  call_.reset();
}

}  // namespace grpc_core
