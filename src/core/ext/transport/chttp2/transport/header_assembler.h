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

#include <grpc/support/port_platform.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/call/message.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace http2 {

// Conventions : If the HeaderAssembler or Client Transport code is doing
// something wrong, we fail with a DCHECK. If the peer sent some bad data, we
// fail with the appropriate Http2Status.

class HeaderAssembler {
 public:
  Http2Status AppendHeaderFrame(Http2HeaderFrame& frame) {
    // Validate current state of Assembler
    if (header_in_progress_) {
      // TODO Get rfc quote
      return Http2Status::Http2StreamError(Http2ErrorCode::kProtocolError, "");
    }
    DCHECK_FALSE(is_ready_);
    DCHECK_EQ(stream_id_, 0);
    DCHECK_EQ(buffer.Length(), 0);

    // Validate input frame
    DCHECK_GT(frame.stream_id, 0);
    DCHECK(!frame.end_stream || (frame.end_stream && frame.end_headers));

    // Manage size constraints
    if constexpr (sizeof(size_t) == 4) {
      if (buffer_.Length() >= UINT32_MAX - frame.payload.Length()) {
        // STREAM_ERROR
        return Http2Status::Status(
            absl::StatusCode::kInternal,
            "Stream Error: SliceBuffer overflow for 32 bit platforms.");
      }
    }

    // Start header workflow
    header_in_progress_ = true;
    stream_id_ = frame.stream_id;

    // Manage payload
    frame.payload.MoveFirstNBytesIntoSliceBuffer(frame.payload.Length(),
                                                 buffer_);

    // Manage if last frame
    if (frame.end_headers) {
      is_ready_ = true;
    }

    return Http2Status::Ok();
  }

  Http2Status AppendContinuationFrame(Http2ContinuationFrame& frame) {
    // Validate current state
    if (!header_in_progress_) {
      // TODO Get rfc quote
      return Http2Status::Http2StreamError(Http2ErrorCode::kProtocolError, "");
    }
    DCHECK_FALSE(is_ready_);
    DCHECK_GT(stream_id_, 0);

    // Validate input frame
    DCHECK_EQ(frame.stream_id, stream_id_);
    DCHECK(!frame.end_stream || (frame.end_stream && frame.end_headers));

    // Manage payload
    frame.payload.MoveFirstNBytesIntoSliceBuffer(frame.payload.Length(),
                                                 buffer_);

    // Manage if last frame
    if (frame.end_headers) {
      is_ready_ = true;
    }
    return Http2Status::Ok();
  }

  void ReadMetadata(HPackParser& parser, bool is_initial_metadata,
                    bool is_client) {
    // Validate
    DCHECK_EQ(header_in_progress_, false);
    DCHECK_EQ(is_ready_, true);

    // Generate the gRPC Metadata from buffer_

    Cleanup();
  }

  HeaderAssembler()
      : header_in_progress_(false), is_ready_(false), stream_id_(0) {}

 private:
  void Cleanup() {
    header_in_progress_ = false;
    is_ready_ = false;
    stream_id_ = 0;
    buffer_.Clear();
  }

  bool header_in_progress_;
  bool is_ready_;
  uint32_t stream_id_;
  SliceBuffer buffer_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HEADER_ASSEMBLER_H
