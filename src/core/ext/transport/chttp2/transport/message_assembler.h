//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/util/ref_counted_ptr.h"
#include "third_party/absl/log/check.h"
#include "third_party/grpc/src/core/call/message.h"
#include "third_party/grpc/src/core/lib/slice/slice_buffer.h"

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
  // Input : The payload must contain the payload from the Http2DataFrame.
  // This function will move the payload into an internal buffer.
  void AppendNewDataFrame(SliceBuffer& payload) {
    DCHECK_GE(payload.Length(), 0);
    payload.MoveFirstNBytesIntoSliceBuffer(payload.Length(), message_buffer_);
    DCHECK_EQ(payload.Length(), 0);
  }
  // Input : This is reference to grpc_message.
  // If we have an entire gRPC message buffered, the message will be moved into
  // grpc_message and the function will return true.
  // If we don't have an entire gRPC message buffered, message will not be
  // edited and the function will return false.
  // We expect the caller to run GenerateMessage in a loop till it returns
  // false.
  bool GenerateMessage(MessageHandle& grpc_message) {
    if (state_ == ReadingState::kNewMessage) {
      if (message_buffer_.Length() <= kGrpcHeaderSizeInBytes) {
        // We don't have enough bytes for a full header.
        // The partial header is saved in message_buffer_.
        // No message should be sent to caller.
        return false;
      }
      header_ = ExtractGrpcHeader(message_buffer_);
      state_ = ReadingState::kCompleteHeader;
    }
    if (state_ == ReadingState::kCompleteHeader &&
        message_buffer_.Length() >= header_.length) {
      // TODO : Special case : if gRPC header has message length 0?
      // Pass it up
      // Max len of a gRPC message. 4 GB. Other stacks fail at 2GB.
      SliceBuffer temp;
      message_buffer_.MoveFirstNBytesIntoSliceBuffer(header_.length, temp);
      grpc_message->payload()->Append(std::move(temp));
      state_ = ReadingState::kNewMessage;
      header_.flags = 0;
      header_.length = 0;
      return true;
    }
    return false;
  }

 public:
  // TODO : Special case : what if we get only half a message and end_Stream?
  // Fail the STREAM.
  enum class ReadingState : uint8_t {
    kNewMessage,
    kCompleteHeader,
  };
  ReadingState state_ = ReadingState::kNewMessage;
  GrpcMessageHeader header_;
  SliceBuffer message_buffer_;
};

/*
  class Http2ClientTransport {
    Stream {
      // ... other stuff ...
      GrpcMessageAssembler assemble_;
    };
  };

  ProcessDataFrame(Http2DataFrame frame) {
    Stream stream = LookUp(frame.stream_id);
    if(stream is valid and not closed) {
      stream.assemble_.AppendNewDataFrame(frame.payload);
      MessageHandler message = MakeNewMessageHandler();
      while(stream.assemble_.GenerateMessage(message)) {
        stream.call_handler.PushMessage(std::move(message));
        message = MakeNewMessageHandler();
      }
    }
  }

*/

GRPC_CHECK_CLASS_SIZE(GrpcMessageAssembler, 10);

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_MESSAGE_ASSEMBLER_H
