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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_MESSAGE_ASSEMBLER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_MESSAGE_ASSEMBLER_H

#include <cstdint>
#include <utility>

#include "absl/log/check.h"
#include "src/core/call/message.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace http2 {

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
  void AppendNewDataFrame(SliceBuffer& payload, bool is_end_of_stream) {
    is_end_of_stream_ = is_end_of_stream;
    DCHECK_GE(payload.Length(), 0);
    payload.MoveFirstNBytesIntoSliceBuffer(payload.Length(), message_buffer_);
    DCHECK_EQ(payload.Length(), 0);
  }

  // We expect the caller to run GenerateMessage in a loop till it returns
  // nullptr or error.
  absl::StatusOr<MessageHandle> GenerateMessage() {
    if (message_buffer_.Length() <= kGrpcHeaderSizeInBytes) {
      header_ = ExtractGrpcHeader(message_buffer_);
      if (message_buffer_.Length() + kGrpcHeaderSizeInBytes >= header_.length) {
        // If gRPC header has length 0, we return an empty message.
        // Bounds: Max len of a valid gRPC message is 4 GB in gRPC C++. 2GB for
        // other stacks. Since 4 bytes can hold length of 4GB, we dont check
        // bounds.
        SliceBuffer discard;
        message_buffer_.MoveFirstNBytesIntoSliceBuffer(kGrpcHeaderSizeInBytes,
                                                       discard);
        discard.Clear();
        SliceBuffer temp;
        message_buffer_.MoveFirstNBytesIntoSliceBuffer(header_.length, temp);
        MessageHandle grpc_message = Arena::MakePooled<Message>();
        grpc_message->payload()->Append(std::move(temp));
        return grpc_message;
      }
      if (is_end_of_stream_ && message_buffer_.Length() > 0) {
        absl::InternalError("Incomplete gRPC frame received");
      }
      return nullptr;
    }

   public:
    bool is_end_of_stream_ = false;
    SliceBuffer message_buffer_;
  };

  GRPC_CHECK_CLASS_SIZE(GrpcMessageAssembler, 10);

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_MESSAGE_ASSEMBLER_H
