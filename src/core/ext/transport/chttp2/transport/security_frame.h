//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_SECURITY_FRAME_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_SECURITY_FRAME_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport_framing_endpoint_extension.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"

namespace grpc_core {

#define GRPC_HTTP2_SECURITY_FRAME_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

// Manages sending and receiving HTTP2 security frames (type 0x08).
// It bridges HTTP2 Transport and TransportFramingEndpointExtension.
class SecurityFrameHandler final : public RefCounted<SecurityFrameHandler> {
 public:
  SecurityFrameHandler() = default;
  ~SecurityFrameHandler() override {
    GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::Destructor";
  };
  SecurityFrameHandler(const SecurityFrameHandler&) = delete;
  SecurityFrameHandler& operator=(const SecurityFrameHandler&) = delete;
  SecurityFrameHandler(SecurityFrameHandler&&) = delete;
  SecurityFrameHandler& operator=(SecurityFrameHandler&&) = delete;

  ///////////////////////////////////////////////////////////////////////////////
  // Initialization

  // SendFrameCallbackFactory is called from the Transport Party.
  // But the callback that it returns is run on some other thread.
  auto SendFrameCallbackFactory(
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine) {
    return [self = this->Ref(), event_engine](SliceBuffer* data) {
      event_engine->Run([self, data = std::move(*data)]() mutable {
        GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::Callback";
        bool call_wakeup = false;
        {
          MutexLock lock(&self->mutex_);
          // In the rare possibility that we receive 2 quick callbacks in
          // succession before the transport is able to read the payload_
          // then we will apply the latest key and discard the old key.
          self->payload_.Clear();
          if (!self->transport_closed_) {
            self->payload_.Swap(&data);
            call_wakeup = true;
          }
        }
        if (call_wakeup) {
          GRPC_HTTP2_SECURITY_FRAME_DLOG
              << "SecurityFrameHandler::Callback Wakeup";
          self->waker_.Wakeup();
        }
      });
    };
  }

  struct EndpointExtensionState {
    bool is_set = false;
  };

  // Only run on the Transport Party
  GRPC_MUST_USE_RESULT EndpointExtensionState Initialize(
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine) {
    GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::Initialize";
    endpoint_extension_ = grpc_event_engine::experimental::QueryExtension<
        TransportFramingEndpointExtension>(event_engine.get());
    if (endpoint_extension_ != nullptr) {
      endpoint_extension_->SetSendFrameCallback(
          SendFrameCallbackFactory(event_engine));
      return EndpointExtensionState{true};
    }
    LOG(ERROR) << "SecurityFrameHandler::Initialize could not initialize "
                  "TransportFramingEndpointExtension";
    return EndpointExtensionState{false};
  }

  ///////////////////////////////////////////////////////////////////////////////
  // When a Security Frame is received by the Transport

  // Only run on the Transport Party
  void ProcessPayload(SliceBuffer&& payload) {
    GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::ProcessPayload";
    if (endpoint_extension_ != nullptr) {
      MutexLock lock(&mutex_);
      if (!transport_closed_) {
        endpoint_extension_->ReceiveFrame(std::move(payload));
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // When a Security Frame needs to be sent by the Transport

  enum class SleepState : uint8_t {
    kWaitingForFrame,  // Sleep until we have a security frame to send
    kWriteOneFrame,
    kScheduledWrite,
    kTransportClosed,
  };

  // Only run on the Transport Party - From SecurityFrameLoop Promise
  auto WaitForSecurityFrameSending() {
    GRPC_DCHECK(endpoint_extension_ != nullptr);
    return [self = this->Ref()]() -> Poll<Empty> {
      GRPC_HTTP2_SECURITY_FRAME_DLOG
          << "SecurityFrameHandler::WaitForSecurityFrameSending";
      if (self->sleep_state_ == SleepState::kTransportClosed) {
        return Empty{};
      } else if (self->sleep_state_ == SleepState::kWaitingForFrame) {
        MutexLock lock(&self->mutex_);
        if (self->payload_.Length() > 0) {
          self->sleep_state_ = SleepState::kWriteOneFrame;
          return Empty{};
        }
      }
      GRPC_HTTP2_SECURITY_FRAME_DLOG
          << "SecurityFrameHandler::WaitForSecurityFrameSending Add Waker";
      self->waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      return Pending{};
    };
  }

  struct TerminateSecurityFrameLoop {
    bool terminate = false;
  };

  // Only run on the Transport Party - From SecurityFrameLoop Promise
  TerminateSecurityFrameLoop TriggerWriteSecurityFrame() {
    GRPC_HTTP2_SECURITY_FRAME_DLOG
        << "SecurityFrameHandler::TriggerWriteSecurityFrame";
    GRPC_DCHECK(endpoint_extension_ != nullptr);
    GRPC_DCHECK(sleep_state_ == SleepState::kWriteOneFrame ||
                sleep_state_ == SleepState::kTransportClosed);
    if (sleep_state_ == SleepState::kWriteOneFrame) {
      sleep_state_ = SleepState::kScheduledWrite;
    }
    return TerminateSecurityFrameLoop{
        (sleep_state_ == SleepState::kTransportClosed)};
  }

  // TODO(tjagtap) [PH2][P5] Simplify WaitForSecurityFrameSending and
  // TriggerWriteSecurityFrame by merging the two.

  // Only run on the Transport Party - From MultiplexerLoop Promise
  void MaybeAppendSecurityFrame(SliceBuffer& outbuf) {
    GRPC_DCHECK(sleep_state_ != SleepState::kWriteOneFrame);
    if (sleep_state_ == SleepState::kScheduledWrite &&
        endpoint_extension_ != nullptr) {
      Http2Frame frame = Http2SecurityFrame();
      {
        MutexLock lock(&mutex_);
        GRPC_DCHECK(payload_.Length() != 0);
        GRPC_HTTP2_SECURITY_FRAME_DLOG
            << "SecurityFrameHandler::MaybeAppendSecurityFrame Write Frame "
               "Length "
            << payload_.Length();
        std::get<Http2SecurityFrame>(frame).payload.Swap(&payload_);
        GRPC_DCHECK(payload_.Length() == 0);
      }
      Serialize(absl::Span<Http2Frame>(&frame, 1), outbuf);
      sleep_state_ = SleepState::kWaitingForFrame;
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Cleanup

  // Only run on the Transport Party
  void OnTransportClosed() {
    GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::OnTransportClosed";
    MutexLock lock(&mutex_);
    transport_closed_ = true;
    sleep_state_ = SleepState::kTransportClosed;
    payload_.Clear();
    waker_.Wakeup();  // To terminate the SecurityFrameLoop
  }

  SleepState TestOnlySleepState() { return sleep_state_; }

  std::string TestOnlyDebugString() {
    std::string sleep_state_str;
    switch (sleep_state_) {
      case SleepState::kWaitingForFrame:
        sleep_state_str = "kWaitingForFrame";
        break;
      case SleepState::kWriteOneFrame:
        sleep_state_str = "kWriteOneFrame";
        break;
      case SleepState::kScheduledWrite:
        sleep_state_str = "kScheduledWrite";
        break;
      case SleepState::kTransportClosed:
        sleep_state_str = "kTransportClosed";
        break;
    }
    MutexLock lock(&mutex_);
    // Do not ever LOG the payload. It has a security key.
    return absl::StrFormat(
        "SecurityFrameHandler{endpoint_extension_=%s, sleep_state_=%s, "
        "payload_length=%d, transport_closed_=%s}",
        endpoint_extension_ == nullptr ? "null" : "non-null", sleep_state_str,
        payload_.Length(), transport_closed_ ? "true" : "false");
  }

 private:
  // Only access endpoint_extension_ from the transport party
  TransportFramingEndpointExtension* endpoint_extension_ = nullptr;

  // Initialized on the transport party and woken up by some other thread.
  Waker waker_;

  Mutex mutex_;
  // Written/Cleared by the other thread, Read and Cleared by transport party.
  SliceBuffer payload_ ABSL_GUARDED_BY(mutex_);
  // Written by transport party, read by both threads.
  bool transport_closed_ ABSL_GUARDED_BY(mutex_) = false;

  // Only access sleep_state_ from the transport party
  SleepState sleep_state_ = SleepState::kWaitingForFrame;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_SECURITY_FRAME_H
