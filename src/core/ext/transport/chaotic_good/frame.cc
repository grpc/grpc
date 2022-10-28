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

#include <limits>

#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
template <typename Metadata>
absl::StatusOr<Arena::PoolPtr<Metadata>> ReadMetadata(HPackParser* parser,
                                                      SliceBuffer slices,
                                                      uint32_t stream_id,
                                                      bool is_header,
                                                      bool is_client) {
  Arena::PoolPtr<Metadata> metadata;
  parser->BeginFrame(
      metadata.get(), std::numeric_limits<uint32_t>::max(),
      is_header ? HPackParser::Boundary::EndOfHeaders
                : HPackParser::Boundary::EndOfStream,
      HPackParser::Priority::None,
      HPackParser::LogInfo{stream_id,
                           is_header ? HPackParser::LogInfo::Type::kHeaders
                                     : HPackParser::LogInfo::Type::kTrailers,
                           is_client});
  for (size_t i = 0; i < slices.Count(); i++) {
    GRPC_RETURN_IF_ERROR(
        parser->Parse(slices.c_slice_at(i), i == slices.Count() - 1));
  }
  parser->FinishFrame();
  return std::move(metadata);
}
}  // namespace

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
    auto r = ReadMetadata<ClientMetadata>(parser, deserializer.ReceiveHeaders(),
                                          header.stream_id, true, true);
    if (!r.ok()) return r.status();
  }
  if (header.flags.is_set(1)) {
    message = GetContext<Arena>()->MakePooled<Message>();
    deserializer.ReceiveMessage().Swap(message->payload());
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
    auto r = ReadMetadata<ServerMetadata>(parser, deserializer.ReceiveHeaders(),
                                          header.stream_id, true, false);
    if (!r.ok()) return r.status();
  }
  if (header.flags.is_set(1)) {
    message = GetContext<Arena>()->MakePooled<Message>();
    deserializer.ReceiveMessage().Swap(message->payload());
  }
  if (header.flags.is_set(2)) {
    auto r = ReadMetadata<ServerMetadata>(
        parser, deserializer.ReceiveTrailers(), header.stream_id, false, false);
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
