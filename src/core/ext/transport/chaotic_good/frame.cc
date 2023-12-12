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

#include <cstdint>
#include <limits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
const NoDestruct<Slice> kZeroSlice{[] {
  // Frame header size is fixed to 24 bytes.
  auto slice = GRPC_SLICE_MALLOC(FrameHeader::frame_header_size_);
  memset(GRPC_SLICE_START_PTR(slice), 0, FrameHeader::frame_header_size_);
  return slice;
}()};

class FrameSerializer {
 public:
  explicit FrameSerializer(FrameType frame_type, uint32_t stream_id,
                           uint32_t message_padding) {
    output_.AppendIndexed(kZeroSlice->Copy());
    header_.type = frame_type;
    header_.stream_id = stream_id;
    header_.message_padding = message_padding;
    header_.flags.SetAll(false);
  }
  // If called, must be called before AddTrailers, Finish.
  SliceBuffer& AddHeaders() {
    header_.flags.set(0);
    return output_;
  }
  // If called, must be called before Finish.
  SliceBuffer& AddTrailers() {
    header_.flags.set(1);
    header_.header_length = output_.Length() - FrameHeader::frame_header_size_;
    return output_;
  }

  SliceBuffer Finish() {
    // Calculate frame header_length or trailer_length if available.
    if (header_.flags.is_set(1)) {
      // Header length is already known in AddTrailers().
      header_.trailer_length = output_.Length() - header_.header_length -
                               FrameHeader::frame_header_size_;
    } else {
      if (header_.flags.is_set(0)) {
        // Calculate frame header length in Finish() since AddTrailers() isn't
        // called.
        header_.header_length =
            output_.Length() - FrameHeader::frame_header_size_;
      }
    }
    header_.Serialize(
        GRPC_SLICE_START_PTR(output_.c_slice_buffer()->slices[0]));
    return std::move(output_);
  }

 private:
  FrameHeader header_;
  SliceBuffer output_;
};

class FrameDeserializer {
 public:
  FrameDeserializer(const FrameHeader& header, SliceBuffer& input)
      : header_(header), input_(input) {}
  const FrameHeader& header() const { return header_; }
  // If called, must be called before ReceiveTrailers, Finish.
  absl::StatusOr<SliceBuffer> ReceiveHeaders() {
    return Take(header_.header_length);
  }
  // If called, must be called before Finish.
  absl::StatusOr<SliceBuffer> ReceiveTrailers() {
    return Take(header_.trailer_length);
  }

  // Return message length to get payload size in data plane.
  uint32_t GetMessageLength() const { return header_.message_length; }
  // Return message padding to get padding size in data plane.
  uint32_t GetMessagePadding() const { return header_.message_padding; }

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
    return std::move(out);
  }
  FrameHeader header_;
  SliceBuffer& input_;
};

template <typename Metadata>
absl::StatusOr<Arena::PoolPtr<Metadata>> ReadMetadata(
    HPackParser* parser, absl::StatusOr<SliceBuffer> maybe_slices,
    uint32_t stream_id, bool is_header, bool is_client,
    absl::BitGenRef bitsrc) {
  if (!maybe_slices.ok()) return maybe_slices.status();
  auto& slices = *maybe_slices;
  auto arena = GetContext<Arena>();
  GPR_ASSERT(arena != nullptr);
  Arena::PoolPtr<Metadata> metadata = arena->MakePooled<Metadata>(arena);
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
    GRPC_RETURN_IF_ERROR(parser->Parse(slices.c_slice_at(i),
                                       i == slices.Count() - 1, bitsrc,
                                       /*call_tracer=*/nullptr));
  }
  parser->FinishFrame();
  return std::move(metadata);
}
}  // namespace

absl::Status SettingsFrame::Deserialize(HPackParser*, const FrameHeader& header,
                                        absl::BitGenRef,
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
  FrameSerializer serializer(FrameType::kSettings, 0, 0);
  return serializer.Finish();
}

std::string SettingsFrame::ToString() const { return "SettingsFrame{}"; }

absl::Status ClientFragmentFrame::Deserialize(HPackParser* parser,
                                              const FrameHeader& header,
                                              absl::BitGenRef bitsrc,
                                              SliceBuffer& slice_buffer) {
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  message_padding = header.message_padding;
  if (header.type != FrameType::kFragment) {
    return absl::InvalidArgumentError("Expected fragment frame");
  }
  FrameDeserializer deserializer(header, slice_buffer);
  if (header.flags.is_set(0)) {
    auto r = ReadMetadata<ClientMetadata>(parser, deserializer.ReceiveHeaders(),
                                          header.stream_id, true, true, bitsrc);
    if (!r.ok()) return r.status();
    if (r.value() != nullptr) {
      headers = std::move(r.value());
    }
  } else if (header.header_length != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected non-zero header length", header.header_length));
  }
  if (header.flags.is_set(1)) {
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
  FrameSerializer serializer(FrameType::kFragment, stream_id, message_padding);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(), serializer.AddHeaders());
  }
  if (end_of_stream) {
    serializer.AddTrailers();
  }
  return serializer.Finish();
}

std::string ClientFragmentFrame::ToString() const {
  return absl::StrCat(
      "ClientFragmentFrame{stream_id=", stream_id, ", headers=",
      headers.get() != nullptr ? headers->DebugString().c_str() : "nullptr",
      ", message=",
      message.get() != nullptr ? message->DebugString().c_str() : "nullptr",
      ", message_padding=", message_padding, ", end_of_stream=", end_of_stream,
      "}");
}

absl::Status ServerFragmentFrame::Deserialize(HPackParser* parser,
                                              const FrameHeader& header,
                                              absl::BitGenRef bitsrc,
                                              SliceBuffer& slice_buffer) {
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  message_padding = header.message_padding;
  FrameDeserializer deserializer(header, slice_buffer);
  if (header.flags.is_set(0)) {
    auto r =
        ReadMetadata<ServerMetadata>(parser, deserializer.ReceiveHeaders(),
                                     header.stream_id, true, false, bitsrc);
    if (!r.ok()) return r.status();
    if (r.value() != nullptr) {
      headers = std::move(r.value());
    }
  } else if (header.header_length != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected non-zero header length", header.header_length));
  }
  if (header.flags.is_set(1)) {
    auto r =
        ReadMetadata<ServerMetadata>(parser, deserializer.ReceiveTrailers(),
                                     header.stream_id, false, false, bitsrc);
    if (!r.ok()) return r.status();
    if (r.value() != nullptr) {
      trailers = std::move(r.value());
    }
  } else if (header.trailer_length != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected non-zero trailer length", header.trailer_length));
  }
  return deserializer.Finish();
}

SliceBuffer ServerFragmentFrame::Serialize(HPackCompressor* encoder) const {
  GPR_ASSERT(stream_id != 0);
  FrameSerializer serializer(FrameType::kFragment, stream_id, message_padding);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(), serializer.AddHeaders());
  }
  if (trailers.get() != nullptr) {
    encoder->EncodeRawHeaders(*trailers.get(), serializer.AddTrailers());
  }
  return serializer.Finish();
}

std::string ServerFragmentFrame::ToString() const {
  return absl::StrCat(
      "ServerFragmentFrame{stream_id=", stream_id, ", headers=",
      headers.get() != nullptr ? headers->DebugString().c_str() : "nullptr",
      ", message=",
      message.get() != nullptr ? message->DebugString().c_str() : "nullptr",
      ", message_padding=", message_padding, ", trailers=",
      trailers.get() != nullptr ? trailers->DebugString().c_str() : "nullptr",
      "}");
}

absl::Status CancelFrame::Deserialize(HPackParser*, const FrameHeader& header,
                                      absl::BitGenRef,
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
  FrameSerializer serializer(FrameType::kCancel, stream_id, 0);
  return serializer.Finish();
}

std::string CancelFrame::ToString() const {
  return absl::StrCat("CancelFrame{stream_id=", stream_id, "}");
}

}  // namespace chaotic_good
}  // namespace grpc_core
