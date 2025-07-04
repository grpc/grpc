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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_MESSAGE_ASSEMBLER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_MESSAGE_ASSEMBLER_H

#include <cstdint>
#include <utility>

#include "absl/log/check.h"
#include "src/core/call/message.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace http2 {

// TODO(tjagtap) TODO(akshitpatel): [PH2][P3] : Write micro benchmarks for
// assembler and disassembler code

constexpr uint32_t kOneGb = (1024u * 1024u * 1024u);

// For the mapping of gRPC Messages to Http2DataFrame, we can have
// 1. One gRPC Message in one Http2DataFrame
// 2. Many gRPC Messages in one Http2DataFrame
// 3. One gRPC Message spread across multiple consecutive Http2DataFrames
// 4. An Http2DataFrame could also hold multiple gRPC Messages with the first
//    and last gRPC Messages being partial messages.
// This class helps to assemble gRPC Messages from a series of Http2DataFrame
// payloads by processing the payloads one at a time.
class GrpcMessageAssembler {
 public:
  // Input : The input must contain the payload from the Http2DataFrame.
  // This function will move the payload into an internal buffer.
  Http2Status AppendNewDataFrame(SliceBuffer& payload,
                                 const bool is_end_stream) {
    DCHECK(!is_end_stream_)
        << "Calling this function when a previous frame was marked as the last "
           "frame does not make sense.";
    is_end_stream_ = is_end_stream;
    if constexpr (sizeof(size_t) == 4) {
      if (GPR_UNLIKELY(message_buffer_.Length() >=
                       UINT32_MAX - payload.Length())) {
        return Http2Status::Http2StreamError(
            Http2ErrorCode::kInternalError,
            "Stream Error: SliceBuffer overflow for 32 bit platforms.");
      }
    }
    payload.MoveFirstNBytesIntoSliceBuffer(payload.Length(), message_buffer_);
    DCHECK_EQ(payload.Length(), 0u);
    return Http2Status::Ok();
  }

  // Returns a valid MessageHandle if it has a complete message.
  // Returns a nullptr if it does not have a complete message.
  // Returns an error if an incomplete message is received and the stream ends.
  ValueOrHttp2Status<MessageHandle> ExtractMessage() {
    const size_t current_len = message_buffer_.Length();
    if (current_len < kGrpcHeaderSizeInBytes) {
      // TODO(tjagtap) : [PH2][P3] : Write a test for this failure.
      LOG(ERROR) << "Incomplete gRPC message received";
      return ReturnNullOrError();
    }
    GrpcMessageHeader header = ExtractGrpcHeader(message_buffer_);
    if constexpr (sizeof(size_t) == 4) {
      if (GPR_UNLIKELY(header.length > kOneGb)) {
        return Http2Status::Http2StreamError(
            Http2ErrorCode::kInternalError,
            "Stream Error: SliceBuffer overflow for 32 bit platforms.");
      }
    }
    if (GPR_LIKELY(current_len - kGrpcHeaderSizeInBytes >= header.length)) {
      SliceBuffer discard;
      message_buffer_.MoveFirstNBytesIntoSliceBuffer(kGrpcHeaderSizeInBytes,
                                                     discard);
      discard.Clear();
      // If gRPC header has length 0, we return an empty message.
      // Bounds: Max len of a valid gRPC message is 4 GB in gRPC C++. 2GB for
      // other stacks. Since 4 bytes can hold length of 4GB, we dont check
      // bounds.
      MessageHandle grpc_message = Arena::MakePooled<Message>();
      message_buffer_.MoveFirstNBytesIntoSliceBuffer(
          header.length, *(grpc_message->payload()));
      uint32_t& flag = grpc_message->mutable_flags();
      flag = header.flags;
      return std::move(grpc_message);
    }
    return ReturnNullOrError();
  }

 private:
  ValueOrHttp2Status<MessageHandle> ReturnNullOrError() {
    if (GPR_UNLIKELY(is_end_stream_ && message_buffer_.Length() > 0)) {
      return Http2Status::Http2StreamError(Http2ErrorCode::kInternalError,
                                           "Incomplete gRPC frame received");
    }
    return ValueOrHttp2Status<MessageHandle>(nullptr);
  }
  bool is_end_stream_ = false;
  SliceBuffer message_buffer_;
};

constexpr uint32_t kMaxMessageBatchSize = (16 * 1024u);

// This class is meant to convert gRPC Messages into Http2DataFrame ensuring
// that the payload size of the data frame is configurable.
// This class is not responsible for queueing or backpressure. That will be done
// by other classes.
// TODO(tjagtap) : [PH2][P2] Edit comment once this
// class is integrated and exercised.
class GrpcMessageDisassembler {
 public:
  // One GrpcMessageDisassembler instance MUST be associated with one stream
  // for its lifetime.
  GrpcMessageDisassembler() = default;

  // GrpcMessageDisassembler object will take ownership of the message.
  void PrepareSingleMessageForSending(MessageHandle message) {
    DCHECK_EQ(GetBufferedLength(), 0u);
    PrepareMessageForSending(std::move(message));
  }

  // GrpcMessageDisassembler object will take ownership of the message.
  void PrepareBatchedMessageForSending(MessageHandle message) {
    PrepareMessageForSending(std::move(message));
    DCHECK_LE(GetBufferedLength(), kMaxMessageBatchSize)
        << "Avoid batches larger than " << kMaxMessageBatchSize << "bytes";
  }

  size_t GetBufferedLength() const { return message_.Length(); }

  // Gets the next Http2DataFrame with a payload of size max_length or lesser.
  Http2DataFrame GenerateNextFrame(const uint32_t stream_id,
                                   const uint32_t max_length,
                                   const bool is_end_stream = false) {
    DCHECK_GT(max_length, 0u);
    DCHECK_GT(GetBufferedLength(), 0u);
    SliceBuffer temp;
    const uint32_t current_length =
        message_.Length() >= max_length ? max_length : message_.Length();
    message_.MoveFirstNBytesIntoSliceBuffer(current_length, temp);
    return Http2DataFrame{stream_id, is_end_stream, std::move(temp)};
  }

  Http2DataFrame GenerateEmptyEndFrame(const uint32_t stream_id) {
    // RFC9113 : Frames with zero length with the END_STREAM flag set (that is,
    // an empty DATA frame) MAY be sent if there is no available space in either
    // flow-control window.
    SliceBuffer temp;
    return Http2DataFrame{stream_id, /*end_stream=*/true, std::move(temp)};
  }

 private:
  void PrepareMessageForSending(MessageHandle message) {
    AppendGrpcHeaderToSliceBuffer(message_, message->flags(),
                                  message->payload()->Length());
    message_.Append(*(message->payload()));
  }

  SliceBuffer message_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_MESSAGE_ASSEMBLER_H
