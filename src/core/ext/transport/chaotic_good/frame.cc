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
const uint8_t kZeros[64] = {};
}

namespace {
const NoDestruct<Slice> kZeroSlice{[] {
  // Frame header size is fixed to 24 bytes.
  auto slice = GRPC_SLICE_MALLOC(FrameHeader::kFrameHeaderSize);
  memset(GRPC_SLICE_START_PTR(slice), 0, FrameHeader::kFrameHeaderSize);
  return slice;
}()};

class FrameSerializer {
 public:
  explicit FrameSerializer(FrameType frame_type, uint32_t stream_id) {
    output_.control.AppendIndexed(kZeroSlice->Copy());
    header_.type = frame_type;
    header_.stream_id = stream_id;
    header_.flags.SetAll(false);
  }

  // If called, must be called before AddTrailers, Finish.
  SliceBuffer& AddHeaders() {
    header_.flags.set(0);
    return output_.control;
  }

  void AddMessage(const FragmentMessage& msg) {
    header_.flags.set(1);
    header_.message_length = msg.length;
    header_.message_padding = msg.padding;
    output_.data = msg.message->payload()->Copy();
    if (msg.padding != 0) {
      output_.data.Append(Slice::FromStaticBuffer(kZeros, msg.padding));
    }
  }

  // If called, must be called before Finish.
  SliceBuffer& AddTrailers() {
    header_.flags.set(2);
    header_.header_length =
        output_.control.Length() - FrameHeader::kFrameHeaderSize;
    return output_.control;
  }

  BufferPair Finish() {
    // Calculate frame header_length or trailer_length if available.
    if (header_.flags.is_set(2)) {
      // Header length is already known in AddTrailers().
      header_.trailer_length = output_.control.Length() -
                               header_.header_length -
                               FrameHeader::kFrameHeaderSize;
    } else {
      if (header_.flags.is_set(0)) {
        // Calculate frame header length in Finish() since AddTrailers() isn't
        // called.
        header_.header_length =
            output_.control.Length() - FrameHeader::kFrameHeaderSize;
      }
    }
    header_.Serialize(
        GRPC_SLICE_START_PTR(output_.control.c_slice_buffer()->slices[0]));
    return std::move(output_);
  }

 private:
  FrameHeader header_;
  BufferPair output_;
};

class FrameDeserializer {
 public:
  FrameDeserializer(const FrameHeader& header, BufferPair& input)
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
    if (input_.control.Length() < length) {
      return absl::InvalidArgumentError(
          "Frame too short (insufficient payload)");
    }
    SliceBuffer out;
    input_.control.MoveFirstNBytesIntoSliceBuffer(length, out);
    return std::move(out);
  }
  FrameHeader header_;
  BufferPair& input_;
};

template <typename Metadata>
absl::StatusOr<Arena::PoolPtr<Metadata>> ReadMetadata(
    HPackParser* parser, absl::StatusOr<SliceBuffer> maybe_slices,
    uint32_t stream_id, bool is_header, bool is_client, absl::BitGenRef bitsrc,
    Arena* arena) {
  if (!maybe_slices.ok()) return maybe_slices.status();
  auto& slices = *maybe_slices;
  GPR_ASSERT(arena != nullptr);
  Arena::PoolPtr<Metadata> metadata = Arena::MakePooled<Metadata>();
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

absl::Status FrameLimits::ValidateMessage(const FrameHeader& header) {
  if (header.message_length > max_message_size) {
    return absl::InvalidArgumentError(
        absl::StrCat("Message length ", header.message_length,
                     " exceeds maximum allowed ", max_message_size));
  }
  if (header.message_padding > max_padding) {
    return absl::InvalidArgumentError(
        absl::StrCat("Message padding ", header.message_padding,
                     " exceeds maximum allowed ", max_padding));
  }
  return absl::OkStatus();
}

absl::Status SettingsFrame::Deserialize(HPackParser* parser,
                                        const FrameHeader& header,
                                        absl::BitGenRef bitsrc, Arena* arena,
                                        BufferPair buffers, FrameLimits) {
  if (header.type != FrameType::kSettings) {
    return absl::InvalidArgumentError("Expected settings frame");
  }
  if (header.flags.is_set(1) || header.flags.is_set(2)) {
    return absl::InvalidArgumentError("Unexpected flags");
  }
  if (buffers.data.Length() != 0) {
    return absl::InvalidArgumentError("Unexpected data");
  }
  FrameDeserializer deserializer(header, buffers);
  if (header.flags.is_set(0)) {
    auto r = ReadMetadata<ClientMetadata>(parser, deserializer.ReceiveHeaders(),
                                          header.stream_id, true, true, bitsrc,
                                          arena);
    if (!r.ok()) return r.status();
    if (r.value() != nullptr) {
      headers = std::move(r.value());
    }
  } else if (header.header_length != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected non-zero header length", header.header_length));
  }
  return deserializer.Finish();
}

BufferPair SettingsFrame::Serialize(HPackCompressor* encoder) const {
  FrameSerializer serializer(FrameType::kSettings, 0);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(), serializer.AddHeaders());
  }
  return serializer.Finish();
}

std::string SettingsFrame::ToString() const { return "SettingsFrame{}"; }

absl::Status ClientFragmentFrame::Deserialize(HPackParser* parser,
                                              const FrameHeader& header,
                                              absl::BitGenRef bitsrc,
                                              Arena* arena, BufferPair buffers,
                                              FrameLimits limits) {
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  if (header.type != FrameType::kFragment) {
    return absl::InvalidArgumentError("Expected fragment frame");
  }
  FrameDeserializer deserializer(header, buffers);
  if (header.flags.is_set(0)) {
    auto r = ReadMetadata<ClientMetadata>(parser, deserializer.ReceiveHeaders(),
                                          header.stream_id, true, true, bitsrc,
                                          arena);
    if (!r.ok()) return r.status();
    if (r.value() != nullptr) {
      headers = std::move(r.value());
    }
  } else if (header.header_length != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected non-zero header length", header.header_length));
  }
  if (header.flags.is_set(1)) {
    auto r = limits.ValidateMessage(header);
    if (!r.ok()) return r;
    message =
        FragmentMessage{Arena::MakePooled<Message>(std::move(buffers.data), 0),
                        header.message_padding, header.message_length};
  } else if (buffers.data.Length() != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected non-zero message length ", buffers.data.Length()));
  }
  if (header.flags.is_set(2)) {
    if (header.trailer_length != 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unexpected trailer length ", header.trailer_length));
    }
    end_of_stream = true;
  } else {
    end_of_stream = false;
  }
  return deserializer.Finish();
}

BufferPair ClientFragmentFrame::Serialize(HPackCompressor* encoder) const {
  GPR_ASSERT(stream_id != 0);
  FrameSerializer serializer(FrameType::kFragment, stream_id);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(), serializer.AddHeaders());
  }
  if (message.has_value()) {
    serializer.AddMessage(message.value());
  }
  if (end_of_stream) {
    serializer.AddTrailers();
  }
  return serializer.Finish();
}

std::string FragmentMessage::ToString() const {
  std::string out =
      absl::StrCat("FragmentMessage{length=", length, ", padding=", padding);
  if (message.get() != nullptr) {
    absl::StrAppend(&out, ", message=", message->DebugString().c_str());
  }
  absl::StrAppend(&out, "}");
  return out;
}

std::string ClientFragmentFrame::ToString() const {
  return absl::StrCat(
      "ClientFragmentFrame{stream_id=", stream_id, ", headers=",
      headers.get() != nullptr ? headers->DebugString().c_str() : "nullptr",
      ", message=", message.has_value() ? message->ToString().c_str() : "none",
      ", end_of_stream=", end_of_stream, "}");
}

absl::Status ServerFragmentFrame::Deserialize(HPackParser* parser,
                                              const FrameHeader& header,
                                              absl::BitGenRef bitsrc,
                                              Arena* arena, BufferPair buffers,
                                              FrameLimits limits) {
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  FrameDeserializer deserializer(header, buffers);
  if (header.flags.is_set(0)) {
    auto r = ReadMetadata<ServerMetadata>(parser, deserializer.ReceiveHeaders(),
                                          header.stream_id, true, false, bitsrc,
                                          arena);
    if (!r.ok()) return r.status();
    if (r.value() != nullptr) {
      headers = std::move(r.value());
    }
  } else if (header.header_length != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected non-zero header length", header.header_length));
  }
  if (header.flags.is_set(1)) {
    auto r = limits.ValidateMessage(header);
    if (!r.ok()) return r;
    message.emplace(Arena::MakePooled<Message>(std::move(buffers.data), 0),
                    header.message_padding, header.message_length);
  } else if (buffers.data.Length() != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected non-zero message length", buffers.data.Length()));
  }
  if (header.flags.is_set(2)) {
    auto r = ReadMetadata<ServerMetadata>(
        parser, deserializer.ReceiveTrailers(), header.stream_id, false, false,
        bitsrc, arena);
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

BufferPair ServerFragmentFrame::Serialize(HPackCompressor* encoder) const {
  GPR_ASSERT(stream_id != 0);
  FrameSerializer serializer(FrameType::kFragment, stream_id);
  if (headers.get() != nullptr) {
    encoder->EncodeRawHeaders(*headers.get(), serializer.AddHeaders());
  }
  if (message.has_value()) {
    serializer.AddMessage(message.value());
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
      ", message=", message.has_value() ? message->ToString().c_str() : "none",
      ", trailers=",
      trailers.get() != nullptr ? trailers->DebugString().c_str() : "nullptr",
      "}");
}

absl::Status CancelFrame::Deserialize(HPackParser*, const FrameHeader& header,
                                      absl::BitGenRef, Arena*,
                                      BufferPair buffers, FrameLimits) {
  if (header.type != FrameType::kCancel) {
    return absl::InvalidArgumentError("Expected cancel frame");
  }
  if (header.flags.any()) {
    return absl::InvalidArgumentError("Unexpected flags");
  }
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  if (buffers.data.Length() != 0) {
    return absl::InvalidArgumentError("Unexpected data");
  }
  FrameDeserializer deserializer(header, buffers);
  stream_id = header.stream_id;
  return deserializer.Finish();
}

BufferPair CancelFrame::Serialize(HPackCompressor*) const {
  GPR_ASSERT(stream_id != 0);
  FrameSerializer serializer(FrameType::kCancel, stream_id);
  return serializer.Finish();
}

std::string CancelFrame::ToString() const {
  return absl::StrCat("CancelFrame{stream_id=", stream_id, "}");
}

}  // namespace chaotic_good
}  // namespace grpc_core
