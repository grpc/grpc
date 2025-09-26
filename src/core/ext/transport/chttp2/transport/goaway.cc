//
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
//

#include "src/core/ext/transport/chttp2/transport/goaway.h"

namespace grpc_core {
namespace http2 {

GoawayManager::GoawayManager(std::unique_ptr<GoawayInterface> goaway_interface)
    : context_(MakeRefCounted<Context>(std::move(goaway_interface))) {}

void GoawayManager::MaybeGetSerializedGoawayFrame(SliceBuffer& output_buf) {
  GRPC_HTTP2_GOAWAY_LOG << "MaybeGetSerializedGoawayFrames: current state: "
                        << context_->GoawayStateToString(
                               context_->goaway_state);
  switch (context_->goaway_state) {
    case GoawayState::kIdle:
    case GoawayState::kDone:
      break;
    case GoawayState::kGracefulGoawayScheduled: {
      DCHECK(!goaway_sent_);
      Http2Frame goaway_frame = context_->GetGracefulGoawayFrame();
      Serialize(absl::Span<Http2Frame>(&goaway_frame, 1), output_buf);
      GRPC_HTTP2_GOAWAY_LOG << "Graceful GOAWAY frame serialized.";
      goaway_sent_ = true;
      break;
    }
    case GoawayState::kGracefulGoawaySent:
    // TODO(akshitpatel) : [PH2][P2] : Currently we are not querying the
    // transport for the last good stream id. Ideally the moment when the
    // graceful GOAWAY frame is sent, the last stream ID passed by the transport
    // should be fine for the final GOAWAY frame. This needs to be verified.
    case GoawayState::kImmediateGoawayRequested: {
      DCHECK(!goaway_sent_);
      Http2Frame goaway_frame = context_->GetGoawayFrame();
      Serialize(absl::Span<Http2Frame>(&goaway_frame, 1), output_buf);
      GRPC_HTTP2_GOAWAY_LOG << "GOAWAY frame serialized.";
      goaway_sent_ = true;
      break;
    }
  }
}

void GoawayManager::NotifyGoawaySent() {
  if (goaway_sent_) {
    GRPC_HTTP2_GOAWAY_LOG << "GOAWAY frame sent in current write cycle.";
    context_->SentGoawayTransition();
    goaway_sent_ = false;
  }
}

inline void GoawayManager::Context::SentGoawayTransition() {
  GRPC_HTTP2_GOAWAY_LOG << "SentGoawayTransition: current state: "
                        << GoawayStateToString(goaway_state);
  switch (goaway_state) {
    case GoawayState::kIdle:
    case GoawayState::kGracefulGoawayScheduled:
    case GoawayState::kDone:
      break;
    case GoawayState::kGracefulGoawaySent:
    case GoawayState::kImmediateGoawayRequested: {
      GRPC_HTTP2_GOAWAY_LOG << "Transitioning to kDone from "
                            << GoawayStateToString(goaway_state);
      goaway_state = GoawayState::kDone;
      WaitSet::WakeupSet wakers_to_wakeup = wakers.TakeWakeupSet();
      wakers_to_wakeup.Wakeup();
      break;
    }
    default:
      break;
  }
}

std::string GoawayManager::Context::GoawayStateToString(
    GoawayState goaway_state) {
  switch (goaway_state) {
    case GoawayState::kIdle:
      return "kIdle";
    case GoawayState::kGracefulGoawayScheduled:
      return "kGracefulGoawayScheduled";
    case GoawayState::kGracefulGoawaySent:
      return "kGracefulGoawaySent";
    case GoawayState::kImmediateGoawayRequested:
      return "kImmediateGoawayRequested";
    case GoawayState::kDone:
      return "kDone";
    default:
      return "unknown";
  }
}

std::optional<Http2Frame> GoawayManager::TestOnlyMaybeGetGoawayFrame() {
  GRPC_HTTP2_GOAWAY_LOG << "MaybeGetSerializedGoawayFrames: current state: "
                        << context_->GoawayStateToString(
                               context_->goaway_state);
  switch (context_->goaway_state) {
    case GoawayState::kIdle:
    case GoawayState::kDone:
      break;
    case GoawayState::kGracefulGoawayScheduled: {
      DCHECK(!goaway_sent_);
      Http2Frame goaway_frame = context_->GetGracefulGoawayFrame();
      GRPC_HTTP2_GOAWAY_LOG << "Graceful GOAWAY frame serialized.";
      goaway_sent_ = true;
      return goaway_frame;
    }
    case GoawayState::kGracefulGoawaySent:
    // TODO(akshitpatel) : [PH2][P2] : Currently we are not querying the
    // transport for the last good stream id. Ideally the moment when the
    // graceful GOAWAY frame is sent, the last stream ID passed by the transport
    // should be fine for the final GOAWAY frame. This needs to be verified.
    case GoawayState::kImmediateGoawayRequested: {
      DCHECK(!goaway_sent_);
      Http2Frame goaway_frame = context_->GetGoawayFrame();
      GRPC_HTTP2_GOAWAY_LOG << "GOAWAY frame serialized.";
      goaway_sent_ = true;
      return goaway_frame;
    }
  }

  return std::nullopt;
}

}  // namespace http2
}  // namespace grpc_core
