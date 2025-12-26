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
#include "absl/types/span.h"

namespace grpc_core {

#define GRPC_HTTP2_SECURITY_FRAME_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

// Manages sending and receiving HTTP2 security frames (type 0x08).
// It bridges HTTP2 Transport and `TransportFramingEndpointExtension.
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

  // EndpointCallbackFactory Promise Factory is called from the Transport Party
  // But the promise that it returns is run on some other thread.
  auto EndpointCallbackFactory(
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine) {
    return [self = this->Ref(), event_engine](SliceBuffer* data) {
      event_engine->Run([self, data]() {
        GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::Callback";
        bool call_wakeup = false;
        {
          MutexLock lock(&self->payload_mutex_);
          // In the rare possibility that we receive 2 quick callbacks in
          // succession before the transport is able to read the payload_ then
          // we will apply the latest key and discard the old one.
          self->payload_.Clear();
          if (!self->transport_closed_) {
            self->payload_.Swap(data);
            call_wakeup = true;
          }
        }
        if (call_wakeup) {
          self->WakeUp();
        }
      });
    };
  }

  // Only run on the Transport Party
  GRPC_MUST_USE_RESULT bool Initialize(
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine) {
    GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::Initialize";
    endpoint_extension_ = grpc_event_engine::experimental::QueryExtension<
        TransportFramingEndpointExtension>(event_engine.get());
    if (endpoint_extension_ != nullptr) {
      endpoint_extension_->SetSendFrameCallback(
          EndpointCallbackFactory(event_engine));
      return true;
    }
    LOG(ERROR) << "SecurityFrameHandler::Initialize could not initialize "
                  "TransportFramingEndpointExtension";
    return false;
  }

  ///////////////////////////////////////////////////////////////////////////////
  // When a Security Frame is received by the Transport

  // Only run on the Transport Party
  void ProcessPayload(SliceBuffer&& payload) {
    GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::ProcessPayload";
    if (endpoint_extension_ != nullptr) {
      endpoint_extension_->ReceiveFrame(std::move(payload));
    }
  }

  ///////////////////////////////////////////////////////////////////////////////
  // When a Security Frame needs to be sent by the Transport

  enum class SleepState : uint8_t {
    kWaitingForFrame,  // Sleep until we have a security frame to send
    kWriteOneFrame,
    kScheduledWrite,
  };

  // Only run on the Transport Party - From SecurityFrameLoop Promise
  auto WaitForSecurityFrameSending() {
    GRPC_DCHECK(endpoint_extension_ != nullptr);
    return [self = this->Ref()]() -> Poll<Empty> {
      GRPC_HTTP2_SECURITY_FRAME_DLOG
          << "SecurityFrameHandler::WaitForSecurityFrameSending";
      if (self->sleep_state_ == SleepState::kWaitingForFrame) {
        MutexLock lock(&self->payload_mutex_);
        if (self->payload_.Length() > 0) {
          self->sleep_state_ = SleepState::kWriteOneFrame;
        }
      }
      if (self->sleep_state_ != SleepState::kWriteOneFrame) {
        GRPC_HTTP2_SECURITY_FRAME_DLOG
            << "SecurityFrameHandler::WaitForSecurityFrameSending Add Waker";
        self->waker_ = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      }
      return Empty{};
    };
  }

  // Only run on the Transport Party - From SecurityFrameLoop Promise
  void TriggerWriteSecurityFrame() {
    GRPC_HTTP2_SECURITY_FRAME_DLOG
        << "SecurityFrameHandler::TriggerWriteSecurityFrame";
    GRPC_DCHECK(endpoint_extension_ != nullptr);
    sleep_state_ = SleepState::kScheduledWrite;
  }

  // Only run on the Transport Party - From MultiplexerLoop Promise
  void MaybeAppendSecurityFrame(SliceBuffer& outbuf) {
    if (sleep_state_ == SleepState::kScheduledWrite &&
        endpoint_extension_ != nullptr) {
      Http2SecurityFrame frame;
      {
        MutexLock lock(&payload_mutex_);
        GRPC_HTTP2_SECURITY_FRAME_DLOG
            << "SecurityFrameHandler::MaybeAppendSecurityFrame Write Frame "
               "Length "
            << payload_.Length();
        GRPC_DCHECK(payload_.Length() != 0);
        frame.payload.Swap(&payload_);
        GRPC_DCHECK(payload_.Length() == 0);
      }
      std::vector<Http2Frame> frames;
      frames.push_back(std::move(frame));
      Serialize(absl::MakeSpan(frames), outbuf);
      sleep_state_ = SleepState::kWaitingForFrame;
    }
  }

  ///////////////////////////////////////////////////////////////////////////////
  // Cleanup

  // Only run on the Transport Party - From CloseTransport Promise
  void OnTransportClosed() {
    GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::OnTransportClosed";
    MutexLock lock(&payload_mutex_);
    transport_closed_ = true;
    payload_.Clear();
  }

 private:
  // NOT run on the Transport Party
  void WakeUp() {
    GRPC_HTTP2_SECURITY_FRAME_DLOG << "SecurityFrameHandler::WakeUp";
    GRPC_DCHECK(endpoint_extension_ != nullptr);
    waker_.Wakeup();
  }

  // Only access endpoint_extension_ from the transport party
  TransportFramingEndpointExtension* endpoint_extension_ = nullptr;

  // Initialized on the transport party and woken up by some other thread.
  Waker waker_;

  Mutex payload_mutex_;
  // Written/Cleared by the other thread, read by transport party.
  SliceBuffer payload_ ABSL_GUARDED_BY(payload_mutex_);
  // Written by transport party, read by both threads.
  bool transport_closed_ ABSL_GUARDED_BY(payload_mutex_) = false;

  // Only access should_sleep_ from the transport party
  SleepState sleep_state_ = SleepState::kWaitingForFrame;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_SECURITY_FRAME_H
