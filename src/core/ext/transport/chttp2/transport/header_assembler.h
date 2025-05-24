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

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
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

// A gRPC client is permitted to send only initial metadata. A gRPC server is
// permitted to send both initial metadata and trailing metadata where initial
// metadata is optional. Hence, the server can receive only 1 HTTP2 Header frame
// and the client can receive atmost 2 HTTP2 header frames.
constexpr uint8_t kMaxHeaderFramesForClientAssembler = 2;
constexpr uint8_t kMaxHeaderFramesForServerAssembler = 1;

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
    // Validate current state of Assembler
    if (GPR_UNLIKELY(header_in_progress_)) {
      Cleanup();
      LOG(ERROR) << "Connection Error: " << kAssemblerContiguousSequenceError;
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          std::string(kAssemblerContiguousSequenceError));
    }

    // Validate input frame
    DCHECK_GT(frame.stream_id, 0u)
        << "RFC9113 : HEADERS frames MUST be associated with a stream.";
    if (GPR_UNLIKELY(frame.stream_id != stream_id_)) {
      Cleanup();
      LOG(ERROR) << "Connection Error: " << kAssemblerContiguousSequenceError;
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          std::string(kAssemblerContiguousSequenceError));
    }

    ++num_headers_received_;
    if (GPR_UNLIKELY(num_headers_received_ > max_headers_)) {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kInternalError,
          std::string("Too many header frames sent by peer"));
    }

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
    // Validate current state
    if (GPR_UNLIKELY(!header_in_progress_)) {
      Cleanup();
      LOG(ERROR) << "Connection Error: " << kAssemblerContiguousSequenceError;
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          std::string(kAssemblerContiguousSequenceError));
    }

    if (GPR_UNLIKELY(is_ready_ == true)) {
      // Received comtinuation frame after END_HEADERS was received. This is
      // wrong.
      Cleanup();
      LOG(ERROR) << "Connection Error: " << kAssemblerContiguousSequenceError;
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          std::string(kAssemblerContiguousSequenceError));
    }

    // Validate input frame
    if (GPR_UNLIKELY(frame.stream_id != stream_id_)) {
      Cleanup();
      LOG(ERROR) << "Connection Error: " << kAssemblerMismatchedStreamId;
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          std::string(kAssemblerMismatchedStreamId));
    }

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

  explicit HeaderAssembler(const uint32_t stream_id, const bool is_client)
      : header_in_progress_(false),
        is_ready_(false),
        num_headers_received_(0),
        max_headers_(is_client ? kMaxHeaderFramesForClientAssembler
                               : kMaxHeaderFramesForServerAssembler),
        stream_id_(stream_id) {}

  ~HeaderAssembler() { buffer_.Clear(); };

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
  uint8_t num_headers_received_;
  const uint8_t max_headers_;
  const uint32_t stream_id_;
  SliceBuffer buffer_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HEADER_ASSEMBLER_H
