// Copyright 2022 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/frame.h"

#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace chaotic_good {

absl::Status SettingsFrame::Deserialize(HPackParser* parser,
                                        const FrameHeader& header,
                                        const SliceBuffer& slice_buffer) {
  if (header.type != FrameType::kSettings) {
    return absl::InvalidArgumentError("Expected settings frame");
  }
  if (header.flags.any()) {
    return absl::InvalidArgumentError("Unexpected flags");
  }
  FrameDeserializer deserializer(header, slice_buffer);
  return deserializer.Finish();
}

SliceBuffer SettingsFrame::Serialize(HPackCompressor*) const {
  FrameSerializer serializer(FrameType::kSettings, BitSet<3>());
  return serializer.Finish();
}

absl::Status ClientFragmentFrame::Deserialize(HPackParser* parser,
                                              const FrameHeader& header,
                                              const SliceBuffer& slice_buffer) {
  if (header.type != FrameType::kFragment) {
    return absl::InvalidArgumentError("Expected fragment frame");
  }
  FrameDeserializer deserializer(header, slice_buffer);
  if (header.flags.is_set(0)) {
    GRPC_RETURN_IF_ERROR(parser->Parse(deserializer.ReceiveHeaders()));
  }
  if (header.flags.is_set(1)) {
    message = deserializer.ReceiveMessage();
  }
  if (header.flags.is_set(2)) {
    if (header.trailer_length != 0) {
      return absl::InvalidArgumentError("Unexpected trailer length");
    }
    end_of_stream = true;
  }
  return deserializer.Finish();
}

SliceBuffer ClientFragmentFrame::Serialize(HPackCompressor* encoder) const {
  BitSet<3> flags;
  flags.set(0, headers.get() != nullptr);
  flags.set(1, message.get() != nullptr);
  flags.set(2, end_of_stream);
  FrameSerializer serializer(FrameType::kFragment, flags);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(),
                              serializer.AddHeaders().c_slice_buffer());
  }
  if (message.get() != nullptr) {
    serializer.AddMessage().Append(*message->payload());
  }
  return serializer.Finish();
}

absl::Status ServerFragmentFrame::Deserialize(HPackParser* parser,
                                              const FrameHeader& header,
                                              const SliceBuffer& slice_buffer) {
  if (header.type != FrameType::kFragment) {
    return absl::InvalidArgumentError("Expected fragment frame");
  }
  FrameDeserializer deserializer(header, slice_buffer);
  if (header.flags.is_set(0)) {
    GRPC_RETURN_IF_ERROR(parser->Parse(deserializer.ReceiveHeaders()));
  }
  if (header.flags.is_set(1)) {
    message = deserializer.ReceiveMessage();
  }
  if (header.flags.is_set(2)) {
    GRPC_RETURN_IF_ERROR(parser->Parse(deserializer.ReceiveTrailers()));
  }
  return deserializer.Finish();
}

SliceBuffer ServerFragmentFrame::Serialize(HPackCompressor* encoder) const {
  BitSet<3> flags;
  flags.set(0, headers.get() != nullptr);
  flags.set(1, message.get() != nullptr);
  flags.set(2, trailers.get() != nullptr);
  FrameSerializer serializer(FrameType::kFragment, flags);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(),
                              serializer.AddHeaders().c_slice_buffer());
  }
  if (message.get() != nullptr) {
    serializer.AddMessage().Append(*message->payload());
  }
  if (trailers.get() != nullptr) {
    encoder->EncodeRawHeaders(*trailers.get(),
                              serializer.AddTrailers().c_slice_buffer());
  }
  return serializer.Finish();
}

SliceBuffer CancelFrame::Serialize(HPackCompressor*) const {
  FrameSerializer serializer(FrameType::kCancel, BitSet<3>());
  return serializer.Finish();
}

}  // namespace chaotic_good
}  // namespace grpc_core
