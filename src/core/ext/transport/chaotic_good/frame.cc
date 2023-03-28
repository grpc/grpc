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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/frame.h"

#include <string.h>

#include <limits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
const NoDestruct<Slice> kZeroSlice{[] {
  auto slice = GRPC_SLICE_MALLOC(64);
  memset(GRPC_SLICE_START_PTR(slice), 0, 64);
  return slice;
}()};

class FrameSerializer {
 public:
  explicit FrameSerializer(FrameType type, uint32_t stream_id)
      : header_{type, {}, stream_id, 0, 0, 0} {
    output_.AppendIndexed(kZeroSlice->Copy());
  }
  // If called, must be called before AddMessage, AddTrailers, Finish
  SliceBuffer& AddHeaders() {
    GPR_ASSERT(last_added_ == nullptr);
    header_.flags.set(0);
    return Start(&header_.header_length);
  }
  // If called, must be called before AddTrailers, Finish
  SliceBuffer& AddMessage() {
    MaybeCommitLast();
    header_.flags.set(1);
    return Start(&header_.message_length);
  }
  // If called, must be called before Finish
  SliceBuffer& AddTrailers() {
    MaybeCommitLast();
    header_.flags.set(2);
    return Start(&header_.trailer_length);
  }

  SliceBuffer Finish() {
    MaybeCommitLast();
    header_.Serialize(
        GRPC_SLICE_START_PTR(output_.c_slice_buffer()->slices[0]));
    return std::move(output_);
  }

 private:
  SliceBuffer& Start(uint32_t* length_field) {
    last_added_ = length_field;
    length_at_last_added_ = output_.Length();
    return output_;
  }

  void MaybeCommitLast() {
    if (last_added_ == nullptr) return;
    *last_added_ = output_.Length() - length_at_last_added_;
    if (output_.Length() % 64 != 0) {
      output_.Append(kZeroSlice->RefSubSlice(0, 64 - output_.Length() % 64));
    }
  }

  FrameHeader header_;

  uint32_t* last_added_ = nullptr;
  size_t length_at_last_added_;
  SliceBuffer output_;
};

class FrameDeserializer {
 public:
  FrameDeserializer(const FrameHeader& header, SliceBuffer& input)
      : header_(header), input_(input) {}
  const FrameHeader& header() const { return header_; }
  // If called, must be called before ReceiveMessage, ReceiveTrailers
  absl::StatusOr<SliceBuffer> ReceiveHeaders() {
    return Take(header_.header_length);
  }
  // If called, must be called before ReceiveTrailers
  absl::StatusOr<SliceBuffer> ReceiveMessage() {
    return Take(header_.message_length);
  }
  // If called, must be called before Finish
  absl::StatusOr<SliceBuffer> ReceiveTrailers() {
    return Take(header_.trailer_length);
  }

  absl::Status Finish() { return absl::OkStatus(); }

 private:
  absl::StatusOr<SliceBuffer> Take(uint32_t length) {
    if (length == 0) return SliceBuffer{};
    if (input_.Length() < length) {
      return absl::InvalidArgumentError(
          "Frame too short (insufficient payload)");
    }
    SliceBuffer out;
    input_.MoveFirstNBytesIntoSliceBuffer(length, out);
    if (length % 64 != 0) {
      const uint32_t padding_length = 64 - length % 64;
      if (input_.Length() < padding_length) {
        return absl::InvalidArgumentError(
            "Frame too short (insufficient padding)");
      }
      uint8_t padding[64];
      input_.MoveFirstNBytesIntoBuffer(padding_length, padding);
      for (uint32_t i = 0; i < padding_length; i++) {
        if (padding[i] != 0) {
          return absl::InvalidArgumentError("Frame padding not zero");
        }
      }
    }
    return std::move(out);
  }
  FrameHeader header_;
  SliceBuffer& input_;
};

template <typename Metadata>
absl::StatusOr<Arena::PoolPtr<Metadata>> ReadMetadata(
    HPackParser* parser, absl::StatusOr<SliceBuffer> maybe_slices,
    uint32_t stream_id, bool is_header, bool is_client) {
  if (!maybe_slices.ok()) return maybe_slices.status();
  auto& slices = *maybe_slices;
  Arena::PoolPtr<Metadata> metadata;
  parser->BeginFrame(
      metadata.get(), std::numeric_limits<uint32_t>::max(),
      std::numeric_limits<uint32_t>::max(),
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

absl::Status SettingsFrame::Deserialize(HPackParser*, const FrameHeader& header,
                                        SliceBuffer& slice_buffer) {
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
  FrameSerializer serializer(FrameType::kSettings, 0);
  return serializer.Finish();
}

absl::Status ClientFragmentFrame::Deserialize(HPackParser* parser,
                                              const FrameHeader& header,
                                              SliceBuffer& slice_buffer) {
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
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
    auto r = deserializer.ReceiveMessage();
    if (!r.ok()) return r.status();
    r->Swap(message->payload());
  }
  if (header.flags.is_set(2)) {
    if (header.trailer_length != 0) {
      return absl::InvalidArgumentError("Unexpected trailer length");
    }
    end_of_stream = true;
  } else {
    end_of_stream = false;
  }
  return deserializer.Finish();
}

SliceBuffer ClientFragmentFrame::Serialize(HPackCompressor* encoder) const {
  GPR_ASSERT(stream_id != 0);
  FrameSerializer serializer(FrameType::kFragment, stream_id);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(), serializer.AddHeaders());
  }
  if (message.get() != nullptr) {
    serializer.AddMessage().Append(*message->payload());
  }
  if (end_of_stream) {
    serializer.AddTrailers();
  }
  return serializer.Finish();
}

absl::Status ServerFragmentFrame::Deserialize(HPackParser* parser,
                                              const FrameHeader& header,
                                              SliceBuffer& slice_buffer) {
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
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
    auto r = deserializer.ReceiveMessage();
    if (!r.ok()) return r.status();
    r->Swap(message->payload());
  }
  if (header.flags.is_set(2)) {
    auto r = ReadMetadata<ServerMetadata>(
        parser, deserializer.ReceiveTrailers(), header.stream_id, false, false);
  }
  return deserializer.Finish();
}

SliceBuffer ServerFragmentFrame::Serialize(HPackCompressor* encoder) const {
  GPR_ASSERT(stream_id != 0);
  FrameSerializer serializer(FrameType::kFragment, stream_id);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(), serializer.AddHeaders());
  }
  if (message.get() != nullptr) {
    serializer.AddMessage().Append(*message->payload());
  }
  if (trailers.get() != nullptr) {
    encoder->EncodeRawHeaders(*trailers.get(), serializer.AddTrailers());
  }
  return serializer.Finish();
}

absl::Status CancelFrame::Deserialize(HPackParser*, const FrameHeader& header,
                                      SliceBuffer& slice_buffer) {
  if (header.type != FrameType::kCancel) {
    return absl::InvalidArgumentError("Expected cancel frame");
  }
  if (header.flags.any()) {
    return absl::InvalidArgumentError("Unexpected flags");
  }
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  FrameDeserializer deserializer(header, slice_buffer);
  stream_id = header.stream_id;
  return deserializer.Finish();
}

SliceBuffer CancelFrame::Serialize(HPackCompressor*) const {
  GPR_ASSERT(stream_id != 0);
  FrameSerializer serializer(FrameType::kCancel, stream_id);
  return serializer.Finish();
}

}  // namespace chaotic_good
}  // namespace grpc_core
