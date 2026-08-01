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
  SendState state = send_state_.load();
  // If the send is still in flight on the transport, wait for completion.
  if (state == SendState::kSendMessageInFlight ||
      state == SendState::kSendMessageInFlightAndHalfCloseRequested) {
    return Pending{};
  }
  // If the send failed or the stream terminated while the send was pending,
  // resolve to failure.
  if (state == SendState::kSendFailed) {
    return Failure{};
  }
  return Success{};
}

Poll<std::optional<std::string>>
XdsStreamingCallPromiseWrapper::PollPullMessage() {
  RecvState recv_state = recv_state_.load();
  switch (recv_state) {
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
  if (recv_state_.load() != RecvState::kReceivedStatus) return Pending{};
  return std::move(status_);
}

void XdsStreamingCallPromiseWrapper::OnRequestSent(bool ok) {
  if (!ok) {
    send_state_.store(SendState::kSendFailed);
  } else {
    SendState state = send_state_.load();
    if (state == SendState::kSendMessageInFlightAndHalfCloseRequested) {
      send_state_.store(SendState::kHalfCloseInFlight);
      call_->SendHalfClose();
    } else if (state == SendState::kSendMessageInFlight) {
      send_state_.store(SendState::kIdle);
    }
  }
  // Wake any waiting send promise.
  send_message_waker_.Wakeup();
}

void XdsStreamingCallPromiseWrapper::OnRecvMessage(absl::string_view payload) {
  recv_message_ = std::string(payload);
  RecvState expected = RecvState::kRecvMessageInFlight;
  recv_state_.compare_exchange_strong(expected, RecvState::kIdle);
  recv_message_waker_.Wakeup();
}

void XdsStreamingCallPromiseWrapper::OnStatusReceived(absl::Status status) {
  status_ = std::move(status);
  RecvState prev_state = recv_state_.exchange(RecvState::kReceivedStatus);
  if (prev_state == RecvState::kRecvMessageInFlight) {
    recv_message_waker_.Wakeup();
  }
  recv_status_waker_.Wakeup();
}

void XdsStreamingCallPromiseWrapper::SendHalfClose() {
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

void XdsStreamingCallPromiseWrapper::Orphaned() {
  // Release the underlying streaming call.
  call_.reset();
}

}  // namespace grpc_core
