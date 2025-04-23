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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HEADER_ASSEMBLER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HEADER_ASSEMBLER_H

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/call/message.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace http2 {

class HeaderAssembler {
 public:
  HeaderAssembler() : in_progress_(false), can_extract_(false), stream_id_(0) {}

  Http2ErrorCode AppendHeaderFrame(Http2HeaderFrame& frame) {
    // Validate input frame
    DCHECK_GT(frame.stream_id, 0);

    // Validate current state
    if (in_progress_) {
      return Http2ErrorCode::kProtocolError;
    }
    DCHECK_EQ(stream_id_, 0);

    // Start header workflow
    in_progress_ = true;
    stream_id_ = frame.stream_id;

    // Manage payload

    // Manage if last frame
    if (frame.end_headers) {
      EndHeader();
    }
    return Http2ErrorCode::kNoError;
  }

  Http2ErrorCode AppendContinuationFrame(Http2ContinuationFrame& frame) {
    // Validate input frame
    DCHECK_EQ(frame.stream_id, stream_id_);

    // Validate current state
    if (!in_progress_) {
      return Http2ErrorCode::kProtocolError;
    }
    DCHECK_GT(stream_id_, 0);

    // Manage payload

    // Manage if last frame
    if (frame.end_headers) {
      EndHeader();
    }
    return Http2ErrorCode::kNoError;
  }

  void GetCompleteHeader() {
    // grpc_core::HPackParser hpack_parser_;
    // Validate
    DCHECK_EQ(in_progress_, false);
    DCHECK_EQ(can_extract_, true);

    // Pass the payload to the Hpack parser.
    // And then return it.
  }

 private:
  void EndHeader() {
    DCHECK_EQ(in_progress_, true);
    DCHECK_GT(stream_id_, 0);
    in_progress_ = false;
    can_extract_ = true;
    stream_id_ = 0;
  }

  bool in_progress_;
  bool can_extract_;
  uint32_t stream_id_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HEADER_ASSEMBLER_H
