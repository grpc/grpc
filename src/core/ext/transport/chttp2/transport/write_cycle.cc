//
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
//

#include "src/core/ext/transport/chttp2/transport/write_cycle.h"

#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace http2 {

// WriteQuota

std::string WriteQuota::DebugString() const {
  return absl::StrCat("WriteQuota{target=", target_write_size_,
                      ", consumed=", bytes_consumed_, "}");
}

// WriteBufferTracker
std::string WriteBufferTracker::DebugString() const {
  return absl::StrCat(
      "WriteBufferTracker{regular_frames_count=", regular_frames_.size(),
      ", urgent_frames_count=", urgent_frames_.size(),
      ", is_first_write=", is_first_write_, "}");
}

// WriteCycle

absl::InlinedVector<Http2Frame, WriteBufferTracker::kInlinedRegularFramesSize>&
WriteCycle::TestOnlyRegularFrames() {
  return write_buffer_tracker_.TestOnlyRegularFrames();
}

absl::InlinedVector<Http2Frame, WriteBufferTracker::kInlinedUrgentFramesSize>&
WriteCycle::TestOnlyUrgentFrames() {
  return write_buffer_tracker_.TestOnlyUrgentFrames();
}

std::string WriteCycle::DebugString() const {
  return absl::StrCat("WriteCycle{quota=", write_quota_.DebugString(),
                      ", tracker=", write_buffer_tracker_.DebugString(), "}");
}

// TransportWriteContext

std::string TransportWriteContext::DebugString() const {
  return absl::StrCat("TransportWriteContext{is_first_write=", is_first_write_,
                      "} ",
                      write_cycle_ ? write_cycle_->DebugString() : "null");
}

PromiseEndpoint::WriteArgs TransportWriteContext::GetWriteArgs(
    const Http2Settings& peer_settings) {
  PromiseEndpoint::WriteArgs args;
  int max_frame_size = peer_settings.preferred_receive_crypto_message_size();
  if (max_frame_size == 0) {
    max_frame_size = INT_MAX;
  }
  args.set_max_frame_size(max_frame_size);
  return args;
}

}  // namespace http2
}  // namespace grpc_core
