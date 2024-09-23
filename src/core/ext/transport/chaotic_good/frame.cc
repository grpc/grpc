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

#include <string.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
const NoDestruct<Slice> kZeroSlice{[] {
  auto slice = GRPC_SLICE_MALLOC(sizeof(FrameHeader::kFrameHeaderSize));
  memset(GRPC_SLICE_START_PTR(slice), 0, sizeof(FrameHeader::kFrameHeaderSize));
  return slice;
}()};

void AddFrame(FrameType frame_type, uint32_t stream_id, SliceBuffer payload,
              uint32_t payload_alignment, BufferPair* out) {
  FrameHeader header;
  header.type = frame_type;
  header.stream_id = stream_id;
  header.payload_length = payload.Length();
  header.payload_connection_id = header.payload_length > 1024 ? 1 : 0;
  header.Serialize(out->control.AddTiny(FrameHeader::kFrameHeaderSize));
  if (header.payload_connection_id == 0) {
    out->control.Append(payload);
  } else {
    out->data.Append(payload);
    if (payload.Length() % payload_alignment != 0) {
      out->data.Append(kZeroSlice->Copy());
    }
  }
}

template <typename F>
void AddInlineFrame(FrameType frame_type, uint32_t stream_id, F gen_frame,
                    BufferPair* out) {
  const size_t header_slice = out->control.AppendIndexed(
      kZeroSlice->RefSubSlice(0, FrameHeader::kFrameHeaderSize));
  const size_t size_before = out->control.Length();
  gen_frame(out->control);
  const size_t size_after = out->control.Length();
  FrameHeader header;
  header.type = frame_type;
  header.stream_id = stream_id;
  header.payload_length = size_after - size_before;
  header.payload_connection_id = 0;
  header.Serialize(const_cast<uint8_t*>(
      GRPC_SLICE_START_PTR(out->control.c_slice_at(header_slice))));
}

template <typename Metadata>
absl::Status ReadMetadata(HPackParser* parser,
                          absl::StatusOr<SliceBuffer> maybe_slices,
                          uint32_t stream_id, bool is_header, bool is_client,
                          absl::BitGenRef bitsrc, Metadata* metadata) {
  if (!maybe_slices.ok()) return maybe_slices.status();
  auto& slices = *maybe_slices;
  *metadata = Arena::MakePooledForOverwrite<
      typename std::remove_reference<decltype(*metadata)>::type>();
  parser->BeginFrame(
      metadata->get(), std::numeric_limits<uint32_t>::max(),
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
  return absl::OkStatus();
}
}  // namespace

absl::Status SettingsFrame::Deserialize(HPackParser* parser,
                                        const FrameHeader& header,
                                        absl::BitGenRef bitsrc,
                                        SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kSettings);
  if (header.stream_id != 0) {
    return absl::InvalidArgumentError("Expected stream id 0");
  }
  return ReadMetadata(parser, std::move(payload), header.stream_id, true, true,
                      bitsrc, &headers);
}

void SettingsFrame::Serialize(HPackCompressor* encoder,
                              bool& saw_encoding_errors,
                              BufferPair* out) const {
  AddInlineFrame(
      FrameType::kSettings, 0,
      [&saw_encoding_errors, encoder, this](SliceBuffer& out) {
        saw_encoding_errors |= !encoder->EncodeRawHeaders(*headers, out);
      },
      out);
}

std::string SettingsFrame::ToString() const {
  return absl::StrCat("SettingsFrame{",
                      headers == nullptr ? "" : headers->DebugString(), "}");
}

absl::Status ClientInitialMetadataFrame::Deserialize(HPackParser* parser,
                                                     const FrameHeader& header,
                                                     absl::BitGenRef bitsrc,
                                                     SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kClientInitialMetadata);
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  return ReadMetadata(parser, std::move(payload), header.stream_id, true, true,
                      bitsrc, &headers);
}

void ClientInitialMetadataFrame::Serialize(HPackCompressor* encoder,
                                           bool& saw_encoding_errors,
                                           BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddInlineFrame(
      FrameType::kSettings, 0,
      [&saw_encoding_errors, encoder, this](SliceBuffer& out) {
        saw_encoding_errors |= !encoder->EncodeRawHeaders(*headers, out);
      },
      out);
}

std::string ClientInitialMetadataFrame::ToString() const {
  return absl::StrCat(
      "ClientInitialMetadataFrame{stream_id=", stream_id,
      ", headers=", headers == nullptr ? "" : headers->DebugString(), "}");
}

absl::Status MessageFrame::Deserialize(HPackParser* parser,
                                       const FrameHeader& header,
                                       absl::BitGenRef bitsrc,
                                       SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kMessage);
  if (header.stream_id == 0) {
    return absl::InvalidArgumentError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  message = Arena::MakePooled<Message>(std::move(payload), 0);
  return absl::OkStatus();
}

void MessageFrame::Serialize(HPackCompressor*, bool&, BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddFrame(FrameType::kMessage, stream_id, message->payload(),
           message->padding(), out);
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

BufferPair ServerFragmentFrame::Serialize(HPackCompressor* encoder,
                                          bool& saw_encoding_errors) const {
  CHECK_NE(stream_id, 0u);
  FrameSerializer serializer(FrameType::kFragment, stream_id);
  if (headers.get() != nullptr) {
    saw_encoding_errors |=
        !encoder->EncodeRawHeaders(*headers.get(), serializer.AddHeaders());
  }
  if (message.has_value()) {
    serializer.AddMessage(message.value());
  }
  if (trailers.get() != nullptr) {
    saw_encoding_errors |=
        !encoder->EncodeRawHeaders(*trailers.get(), serializer.AddTrailers());
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

BufferPair CancelFrame::Serialize(HPackCompressor*, bool&) const {
  CHECK_NE(stream_id, 0u);
  FrameSerializer serializer(FrameType::kCancel, stream_id);
  return serializer.Finish();
}

std::string CancelFrame::ToString() const {
  return absl::StrCat("CancelFrame{stream_id=", stream_id, "}");
}

}  // namespace chaotic_good
}  // namespace grpc_core
