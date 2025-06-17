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

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/shared_bit_gen.h"

// TODO(tjagtap) TODO(akshitpatel): [PH2][P3] : Write micro benchmarks for
// assembler and disassembler code

namespace grpc_core {
namespace http2 {

// If the Client Transport code is using HeaderAssembler in the wrong way,
// we fail with a DCHECK.
// If the peer sent some bad data, we fail with the appropriate Http2Status.

#define ASSEMBLER_LOG DVLOG(3)

constexpr absl::string_view kAssemblerContiguousSequenceError =
    "RFC9113 : Field blocks MUST be transmitted as a contiguous sequence "
    "of frames, with no interleaved frames of any other type or from any "
    "other stream.";

constexpr absl::string_view kAssemblerMismatchedStreamId =
    "CONTINUATION frame has a different Stream Identifier than the preceeding "
    "HEADERS frame.";

constexpr absl::string_view kAssemblerHpackError =
    "RFC9113 : A decoding error in a field block MUST be treated as a "
    "connection error of type COMPRESSION_ERROR.";

constexpr absl::string_view kGrpcErrorMaxTwoHeaderFrames =
    "Too many header frames sent by peer";

// A gRPC server is permitted to send both initial metadata and trailing
// metadata where initial metadata is optional. A gRPC C++ client is permitted
// to send only initial metadata. However, other gRPC Client implementations may
// send trailing metadata too. So we allow only a maximum of 2 metadata per
// streams. Which means only 2 HEADER frames are legal per stream.
constexpr uint8_t kMaxHeaderFrames = 2;

// TODO(tjagtap) : [PH2][P3] : Handle the case where a Server receives two
// header frames. Which means that the client sent trailing metadata. While we
// dont expect a gRPC C++ peer to behave like this, this might break interop
// tests and genuine interop cases.

// RFC9113
// https://www.rfc-editor.org/rfc/rfc9113.html#name-field-section-compression-a
// A complete field section (which contains our gRPC Metadata) consists of
// either: a single HEADERS or PUSH_PROMISE frame, with the END_HEADERS flag
// set, or a HEADERS or PUSH_PROMISE frame with the END_HEADERS flag unset
// and one or more CONTINUATION frames, where the last CONTINUATION frame
// has the END_HEADERS flag set.
//
// Each field block is processed as a discrete unit. Field blocks MUST be
// transmitted as a contiguous sequence of frames, with no interleaved
// frames of any other type or from any other stream. The last frame in a
// sequence of HEADERS or CONTINUATION frames has the END_HEADERS flag set.
//
// This class will first assemble all the header data into one SliceBuffer
// from each frame. And when END_HEADERS is received, the caller can generate
// the gRPC Metadata.
class HeaderAssembler {
 public:
  // Call this for each incoming HTTP2 Header frame.
  // The payload of the Http2HeaderFrame will be cleared in this function.
  Http2Status AppendHeaderFrame(Http2HeaderFrame&& frame) {
    // Validate input frame
    DCHECK_GT(frame.stream_id, 0u)
        << "RFC9113 : HEADERS frames MUST be associated with a stream.";

    // Manage size constraints
    const size_t current_len = frame.payload.Length();
    if constexpr (sizeof(size_t) == 4) {
      if (GPR_UNLIKELY(buffer_.Length() >= UINT32_MAX - current_len)) {
        Cleanup();
        LOG(ERROR)
            << "Stream Error: SliceBuffer overflow for 32 bit platforms.";
        return Http2Status::Http2StreamError(
            Http2ErrorCode::kInternalError,
            "Stream Error: SliceBuffer overflow for 32 bit platforms.");
      }
    }

    // Start header workflow
    header_in_progress_ = true;

    // Manage payload
    frame.payload.MoveFirstNBytesIntoSliceBuffer(current_len, buffer_);
    ASSEMBLER_LOG << "AppendHeaderFrame " << current_len << " Bytes.";

    // Manage if last frame
    if (frame.end_headers) {
      ASSEMBLER_LOG << "AppendHeaderFrame end_headers";
      is_ready_ = true;
    }

    return Http2Status::Ok();
  }

  // Call this for each incoming HTTP2 Continuation frame.
  // The payload of the Http2ContinuationFrame will be cleared in this function.
  Http2Status AppendContinuationFrame(Http2ContinuationFrame&& frame) {
    // Manage payload
    const size_t current_len = frame.payload.Length();
    frame.payload.MoveFirstNBytesIntoSliceBuffer(current_len, buffer_);
    ASSEMBLER_LOG << "AppendContinuationFrame " << current_len << " Bytes.";

    // Manage if last frame
    if (frame.end_headers) {
      ASSEMBLER_LOG << "AppendHeaderFrame end_headers";
      is_ready_ = true;
    }

    return Http2Status::Ok();
  }

  // The caller MUST check using IsReady() before calling this function
  ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>> ReadMetadata(
      HPackParser& parser, bool is_initial_metadata, bool is_client) {
    ASSEMBLER_LOG << "ReadMetadata " << buffer_.Length() << " Bytes.";

    // Validate
    DCHECK_EQ(is_ready_, true);

    // Generate the gRPC Metadata from buffer_
    // RFC9113 :  A receiver MUST terminate the connection with a connection
    // error (Section 5.4.1) of type COMPRESSION_ERROR if it does not decompress
    // a field block. A decoding error in a field block MUST be treated as a
    // connection error (Section 5.4.1) of type COMPRESSION_ERROR.
    Arena::PoolPtr<grpc_metadata_batch> metadata =
        Arena::MakePooledForOverwrite<grpc_metadata_batch>();
    parser.BeginFrame(
        /*grpc_metadata_batch*/ metadata.get(),
        // TODO(tjagtap) : [PH2][P2] : Manage limits
        /*metadata_size_soft_limit*/ std::numeric_limits<uint32_t>::max(),
        /*metadata_size_hard_limit*/ std::numeric_limits<uint32_t>::max(),
        is_initial_metadata ? HPackParser::Boundary::EndOfHeaders
                            : HPackParser::Boundary::EndOfStream,
        HPackParser::Priority::None,
        HPackParser::LogInfo{stream_id_,
                             is_initial_metadata
                                 ? HPackParser::LogInfo::Type::kHeaders
                                 : HPackParser::LogInfo::Type::kTrailers,
                             is_client});
    for (size_t i = 0; i < buffer_.Count(); i++) {
      absl::Status result = parser.Parse(
          buffer_.c_slice_at(i), i == buffer_.Count() - 1, SharedBitGen(),
          /*call_tracer=*/nullptr);
      if (GPR_UNLIKELY(!result.ok())) {
        Cleanup();
        LOG(ERROR) << "Connection Error: " << kAssemblerHpackError;
        return Http2Status::Http2ConnectionError(
            Http2ErrorCode::kCompressionError,
            std::string(kAssemblerHpackError));
      }
    }
    parser.FinishFrame();

    Cleanup();

    return ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>(
        std::move(metadata));
  }

  size_t GetBufferedHeadersLength() const { return buffer_.Length(); }

  // This value MUST be checked before calling ReadMetadata()
  bool IsReady() const { return is_ready_; }

  explicit HeaderAssembler(const uint32_t stream_id)
      : header_in_progress_(false), is_ready_(false), stream_id_(stream_id) {}

  ~HeaderAssembler() = default;

  HeaderAssembler(HeaderAssembler&& rvalue) = delete;
  HeaderAssembler& operator=(HeaderAssembler&& rvalue) = delete;
  HeaderAssembler(const HeaderAssembler&) = delete;
  HeaderAssembler& operator=(const HeaderAssembler&) = delete;

 private:
  void Cleanup() {
    buffer_.Clear();
    header_in_progress_ = false;
    is_ready_ = false;
  }

  bool header_in_progress_;
  bool is_ready_;
  const uint32_t stream_id_;
  SliceBuffer buffer_;
};

class HeaderDisassembler {
 public:
  // This function will take ownership of metadata object
  // The method will return false if encoding fails. A failed encoding operation
  // is MUST be treated as a connection error by the caller.
  // This function can queue a trailing metadata for sending even before the
  // initial metadata has been extracted.
  bool PrepareForSending(Arena::PoolPtr<grpc_metadata_batch>&& metadata,
                         HPackCompressor& encoder) {
    // Validate disassembler state
    DCHECK(!is_done_);
    // Prepare metadata for sending
    return encoder.EncodeRawHeaders(*metadata.get(), buffer_);
  }

  Http2Frame GetNextFrame(const uint32_t max_frame_length,
                          bool& out_end_headers) {
    if (buffer_.Length() == 0 || is_done_) {
      DCHECK(false) << "Calling code must check size using HasMoreData() "
                       "before GetNextFrame()";
    }
    out_end_headers = buffer_.Length() <= max_frame_length;
    SliceBuffer temp;
    if (out_end_headers) {
      is_done_ = true;
      temp.Swap(&buffer_);
    } else {
      buffer_.MoveFirstNBytesIntoSliceBuffer(max_frame_length, temp);
    }
    if (!did_send_header_frame_) {
      did_send_header_frame_ = true;
      return Http2HeaderFrame{stream_id_, out_end_headers, end_stream_,
                              std::move(temp)};
    } else {
      return Http2ContinuationFrame{stream_id_, out_end_headers,
                                    std::move(temp)};
    }
  }

  bool HasMoreData() const { return !is_done_ && buffer_.Length() > 0; }

  // This number can be used for backpressure related calculations.
  size_t GetBufferedLength() const { return buffer_.Length(); }

  // A separate HeaderDisassembler object MUST be made for Initial Metadata and
  // Trailing Metadata
  explicit HeaderDisassembler(const uint32_t stream_id,
                              const bool is_trailing_metadata)
      : stream_id_(stream_id),
        end_stream_(is_trailing_metadata),
        did_send_header_frame_(false),
        is_done_(false) {}

  ~HeaderDisassembler() = default;

  HeaderDisassembler(HeaderDisassembler&& rvalue) = delete;
  HeaderDisassembler& operator=(HeaderDisassembler&& rvalue) = delete;
  HeaderDisassembler(const HeaderDisassembler&) = delete;
  HeaderDisassembler& operator=(const HeaderDisassembler&) = delete;

  size_t TestOnlyGetMainBufferLength() const { return buffer_.Length(); }

 private:
  const uint32_t stream_id_;
  const bool end_stream_;
  bool did_send_header_frame_;
  bool is_done_;  // Protect against the same disassembler from being used twice

  SliceBuffer buffer_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HEADER_ASSEMBLER_H
