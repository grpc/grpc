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
#include <string>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/status_helper.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

// TODO(tjagtap) TODO(akshitpatel): [PH2][P3] : Write micro benchmarks for
// assembler and disassembler code

namespace grpc_core {
namespace http2 {

// If the Client Transport code is using HeaderAssembler in the wrong way,
// we fail with a GRPC_DCHECK.
// If the peer sent some bad data, we fail with the appropriate Http2Status.

#define ASSEMBLER_LOG DVLOG(3)

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
  // If AppendHeaderFrame returns an OK status, the payload will be consumed.
  // Else, the payload will be left in the frame.
  Http2Status AppendHeaderFrame(Http2HeaderFrame& frame) {
    // Validate input frame
    GRPC_DCHECK_GT(frame.stream_id, 0u)
        << "RFC9113 : HEADERS frames MUST be associated with a stream.";

    // Validate assembler state
    GRPC_DCHECK(!header_in_progress_);

    // Manage size constraints
    const size_t current_len = frame.payload.Length();
    if constexpr (sizeof(size_t) == 4) {
      if (GPR_UNLIKELY(buffer_.Length() >= UINT32_MAX - current_len)) {
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
    ASSEMBLER_LOG << "HeaderAssembler::AppendHeaderFrame " << current_len
                  << " Bytes.";

    // Manage if last frame
    if (frame.end_headers) {
      ASSEMBLER_LOG << "HeaderAssembler::AppendHeaderFrame end_headers";
      is_ready_ = true;
    }

    return Http2Status::Ok();
  }

  // Call this for each incoming HTTP2 Continuation frame.
  // If AppendContinuationFrame returns an OK status, the payload will be
  // consumed. Else, the payload will be left in the frame.
  Http2Status AppendContinuationFrame(Http2ContinuationFrame& frame) {
    // Validate Assembler state
    GRPC_DCHECK(header_in_progress_);

    // Manage payload
    const size_t current_len = frame.payload.Length();
    frame.payload.MoveFirstNBytesIntoSliceBuffer(current_len, buffer_);
    ASSEMBLER_LOG << "HeaderAssembler::AppendContinuationFrame " << current_len
                  << " Bytes.";

    // Manage if last frame
    if (frame.end_headers) {
      ASSEMBLER_LOG << "HeaderAssembler::AppendContinuationFrame end_headers";
      is_ready_ = true;
    }

    return Http2Status::Ok();
  }

  // The caller MUST check using IsReady() before calling this function
  ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>> ReadMetadata(
      HPackParser& parser, bool is_initial_metadata, bool is_client,
      const uint32_t max_header_list_size_soft_limit,
      const uint32_t max_header_list_size_hard_limit) {
    ASSEMBLER_LOG << "HeaderAssembler::ReadMetadata " << buffer_.Length()
                  << " Bytes.";

    // Validate
    GRPC_DCHECK_EQ(is_ready_, true);
    GRPC_DCHECK_GT(stream_id_, 0u)
        << "Stream id must be set before reading metadata.";

    // Generate the gRPC Metadata from buffer_
    // RFC9113 :  A receiver MUST terminate the connection with a connection
    // error (Section 5.4.1) of type COMPRESSION_ERROR if it does not decompress
    // a field block. A decoding error in a field block MUST be treated as a
    // connection error (Section 5.4.1) of type COMPRESSION_ERROR.
    Arena::PoolPtr<grpc_metadata_batch> metadata =
        Arena::MakePooledForOverwrite<grpc_metadata_batch>();
    // TODO(tjagtap) : [PH2][P5] : Currently the transport does not enforce
    // setting allow_true_binary_metadata_ sent to the peer.
    // Ideally Hpack code must validate and enforce this setting but it is not
    // doing so now. Given that this is complex code and also common between
    // CHTTP2 and PH2, we must do this as a standalone project with an
    // experiment of its own. For now we just honour allow_true_binary_metadata_
    // while writing frames for the peer on the write path. We do not enforce it
    // on the read path.
    Http2Status status = ParseHeader(
        parser, std::move(buffer_), metadata.get(),
        ParseHeaderArgs{
            /*is_initial_metadata=*/is_initial_metadata,
            /*is_end_headers=*/is_ready_,
            /*is_client=*/is_client,
            /*max_header_list_size_soft_limit=*/max_header_list_size_soft_limit,
            /*max_header_list_size_hard_limit=*/max_header_list_size_hard_limit,
            /*stream_id=*/stream_id_,
        });
    Cleanup();
    if (!status.IsOk()) {
      return std::move(status);
    }
    return ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>(
        std::move(metadata));
  }

  Http2Status ParseAndDiscardHeaders(
      HPackParser& parser, const bool is_initial_metadata, const bool is_client,
      const uint32_t max_header_list_size_soft_limit,
      const uint32_t max_header_list_size_hard_limit) {
    ASSEMBLER_LOG << "HeaderAssembler::ParseAndDiscardHeaders "
                  << buffer_.Length() << " Bytes"
                  << " is_initial_metadata: " << is_initial_metadata
                  << " is_client: " << is_client
                  << " max_header_list_size_soft_limit: "
                  << max_header_list_size_soft_limit
                  << "max_header_list_size_hard_limit: "
                  << max_header_list_size_hard_limit;
    Http2Status status = ParseHeader(
        parser, std::move(buffer_), /*grpc_metadata_batch=*/nullptr,
        ParseHeaderArgs{
            /*is_initial_metadata=*/is_initial_metadata,
            /*is_end_headers=*/is_ready_,
            /*is_client=*/is_client,
            /*max_header_list_size_soft_limit=*/max_header_list_size_soft_limit,
            /*max_header_list_size_hard_limit=*/max_header_list_size_hard_limit,
            /*stream_id=*/stream_id_,
        });
    Cleanup();
    return status;
  }

  size_t GetBufferedHeadersLength() const { return buffer_.Length(); }

  // This value MUST be checked before calling ReadMetadata()
  bool IsReady() const { return is_ready_; }

  explicit HeaderAssembler()
      : header_in_progress_(false),
        is_ready_(false),
        allow_true_binary_metadata_acked_(true),
        stream_id_(0) {}

  ~HeaderAssembler() = default;

  HeaderAssembler(HeaderAssembler&& rvalue) = delete;
  HeaderAssembler& operator=(HeaderAssembler&& rvalue) = delete;
  HeaderAssembler(const HeaderAssembler&) = delete;
  HeaderAssembler& operator=(const HeaderAssembler&) = delete;

  void InitializeStream(const uint32_t stream_id,
                        const bool allow_true_binary_metadata_acked) {
    GRPC_DCHECK_EQ(stream_id_, 0u);
    GRPC_DCHECK_NE(stream_id, 0u);
    stream_id_ = stream_id;
    allow_true_binary_metadata_acked_ = allow_true_binary_metadata_acked;
  }

  // HPACK parser helpers
  struct ParseHeaderArgs {
    bool is_initial_metadata;
    bool is_end_headers;
    bool is_client;
    uint32_t max_header_list_size_soft_limit;
    uint32_t max_header_list_size_hard_limit;
    uint32_t stream_id;

    std::string DebugString() const {
      return absl::StrCat(
          "is_initial_metadata: ", is_initial_metadata,
          " is_end_headers: ", is_end_headers, " is_client: ", is_client,
          " max_header_list_size_soft_limit: ", max_header_list_size_soft_limit,
          " max_header_list_size_hard_limit: ",
          max_header_list_size_hard_limit);
    }
  };

  static Http2Status ParseHeader(HPackParser& parser, SliceBuffer&& buffer,
                                 grpc_metadata_batch* grpc_metadata_batch,
                                 const ParseHeaderArgs args) {
    parser.BeginFrame(
        /*grpc_metadata_batch*/ grpc_metadata_batch,
        args.max_header_list_size_soft_limit,
        args.max_header_list_size_hard_limit,
        GetBoundary(args.is_initial_metadata, args.is_end_headers),
        HPackParser::Priority::None,
        HPackParser::LogInfo{args.stream_id,
                             args.is_initial_metadata
                                 ? HPackParser::LogInfo::Type::kHeaders
                                 : HPackParser::LogInfo::Type::kTrailers,
                             args.is_client});
    // TODO(tjagtap) [PH2][P5] Bug fix : Check if the received metadata honours
    // allow_true_binary_metadata or not. Will need changes to HPack code.
    absl::Status stream_error = absl::OkStatus();
    for (size_t i = 0; i < buffer.Count(); i++) {
      absl::Status result = parser.Parse(
          buffer.c_slice_at(i), i == buffer.Count() - 1, SharedBitGen(),
          /*call_tracer=*/nullptr);
      if (GPR_UNLIKELY(!result.ok())) {
        parser.StopBufferingFrame();
        intptr_t unused;
        if (grpc_error_get_int(result, StatusIntProperty::kStreamId, &unused)) {
          // We need to keep parsing the other slices in slice buffer. Because
          // HPACK state needs to be maintained for the other streams. Store the
          // error and continue.
          if (stream_error.ok()) {
            stream_error = result;
          }
        } else {
          LOG(ERROR) << "HPack Error: " << result;
          return ToHttpOkOrConnError(result);
        }
      }
    }
    if (GPR_UNLIKELY(!stream_error.ok())) {
      return Http2Status::AbslStreamError(stream_error.code(),
                                          std::string(stream_error.message()));
    }
    parser.FinishFrame();
    return Http2Status::Ok();
  }

  static HPackParser::Boundary GetBoundary(const bool is_initial_metadata,
                                           const bool is_end_headers) {
    HPackParser::Boundary boundary = HPackParser::Boundary::None;
    if (is_end_headers) {
      if (is_initial_metadata) {
        boundary = HPackParser::Boundary::EndOfHeaders;
      } else {
        boundary = HPackParser::Boundary::EndOfStream;
      }
    }

    return boundary;
  }

 private:
  void Cleanup() {
    buffer_.Clear();
    header_in_progress_ = false;
    is_ready_ = false;
  }

  bool header_in_progress_;
  bool is_ready_;
  bool allow_true_binary_metadata_acked_;
  uint32_t stream_id_;
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
    GRPC_DCHECK(!is_done_);
    // Prepare metadata for sending
    return encoder.EncodeRawHeaders(*metadata.get(), buffer_,
                                    allow_true_binary_metadata_peer_);
  }

  Http2Frame GetNextFrame(const uint32_t max_frame_length,
                          bool& out_end_headers) {
    GRPC_DCHECK_GT(stream_id_, 0u) << "Stream id must be set before getting "
                                      "next Header/Continuation frame.";

    if (buffer_.Length() == 0 || is_done_) {
      GRPC_DCHECK(false) << "Calling code must check size using HasMoreData() "
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
  explicit HeaderDisassembler(const bool is_trailing_metadata)
      : stream_id_(0),
        end_stream_(is_trailing_metadata),
        did_send_header_frame_(false),
        is_done_(false),
        allow_true_binary_metadata_peer_(false) {}

  ~HeaderDisassembler() = default;

  HeaderDisassembler(HeaderDisassembler&& rvalue) = delete;
  HeaderDisassembler& operator=(HeaderDisassembler&& rvalue) = delete;
  HeaderDisassembler(const HeaderDisassembler&) = delete;
  HeaderDisassembler& operator=(const HeaderDisassembler&) = delete;

  size_t TestOnlyGetMainBufferLength() const { return buffer_.Length(); }
  void Initialize(const uint32_t stream_id,
                  const bool allow_true_binary_metadata_peer) {
    GRPC_DCHECK_EQ(stream_id_, 0u);
    GRPC_DCHECK_NE(stream_id, 0u);
    stream_id_ = stream_id;
    allow_true_binary_metadata_peer_ = allow_true_binary_metadata_peer;
  }

 private:
  uint32_t stream_id_;
  const bool end_stream_;
  bool did_send_header_frame_;
  bool is_done_;  // Protect against the same disassembler from being used twice
  bool allow_true_binary_metadata_peer_;
  SliceBuffer buffer_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HEADER_ASSEMBLER_H
