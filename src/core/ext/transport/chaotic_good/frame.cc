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

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/bitset.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/status_helper.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
const NoDestruct<Slice> kZeroHeader{[] {
  auto slice = GRPC_SLICE_MALLOC(FrameHeader::kFrameHeaderSize);
  memset(GRPC_SLICE_START_PTR(slice), 0, FrameHeader::kFrameHeaderSize);
  return slice;
}()};
const NoDestruct<Slice> kZeroSlice{[] {
  auto slice = GRPC_SLICE_MALLOC(64);
  memset(GRPC_SLICE_START_PTR(slice), 0, GRPC_SLICE_LENGTH(slice));
  return slice;
}()};

void AddFrame(FrameType frame_type, uint32_t stream_id, const SliceBuffer& payload,
              uint32_t alignment, BufferPair* out) {
  FrameHeader header;
  header.type = frame_type;
  header.stream_id = stream_id;
  header.payload_length = payload.Length();
  header.payload_connection_id = header.payload_length > 1024 ? 1 : 0;
  LOG(INFO) << "Serialize header: " << header;
  header.Serialize(out->control.AddTiny(FrameHeader::kFrameHeaderSize));
  if (header.payload_connection_id == 0) {
    out->control.Append(payload);
  } else {
    out->data.Append(payload);
    out->data.Append(kZeroSlice->RefSubSlice(0, header.Padding(alignment)));
  }
}

template <typename F>
void AddInlineFrame(FrameType frame_type, uint32_t stream_id, F gen_frame,
                    BufferPair* out) {
  const size_t header_slice = out->control.AppendIndexed(
      kZeroHeader->Copy());
  const size_t size_before = out->control.Length();
  gen_frame(out->control);
  const size_t size_after = out->control.Length();
  FrameHeader header;
  header.type = frame_type;
  header.stream_id = stream_id;
  header.payload_length = size_after - size_before;
  header.payload_connection_id = 0;
  LOG(INFO) << "Serialize header: " << header;
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
      typename std::remove_reference<decltype(**metadata)>::type>();
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

absl::Status SettingsFrame::Deserialize(const DeserializeContext& ctx,
                                        const FrameHeader& header,
                                        SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kSettings);
  if (header.stream_id != 0) {
    return absl::InternalError("Expected stream id 0");
  }
  return ReadMetadata(ctx.parser, std::move(payload), header.stream_id, true, true,
                      ctx.bitsrc, &headers);
}

void SettingsFrame::Serialize(const SerializeContext& ctx,
                              BufferPair* out) const {
  AddInlineFrame(
      FrameType::kSettings, 0,
      [&ctx, this](SliceBuffer& out) {
        if (headers == nullptr) return;
        ctx.saw_encoding_errors |= !ctx.encoder->EncodeRawHeaders(*headers, out);
      },
      out);
}

std::string SettingsFrame::ToString() const {
  return absl::StrCat("SettingsFrame{",
                      headers == nullptr ? "" : headers->DebugString(), "}");
}

absl::Status ClientInitialMetadataFrame::Deserialize(const DeserializeContext& ctx,
                                                     const FrameHeader& header,
                                                     SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kClientInitialMetadata);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  return ReadMetadata(ctx.parser, std::move(payload), header.stream_id, true, true,
                      ctx.bitsrc, &headers);
}

void ClientInitialMetadataFrame::Serialize(const SerializeContext& ctx,
                                           BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddInlineFrame(
      FrameType::kClientInitialMetadata, stream_id,
      [&ctx, this](SliceBuffer& out) {
        ctx.saw_encoding_errors |= !ctx.encoder->EncodeRawHeaders(*headers, out);
      },
      out);
}

std::string ClientInitialMetadataFrame::ToString() const {
  return absl::StrCat(
      "ClientInitialMetadataFrame{stream_id=", stream_id,
      ", headers=", headers == nullptr ? "" : headers->DebugString(), "}");
}

absl::Status ClientEndOfStream::Deserialize(const DeserializeContext& ctx,
                                            const FrameHeader& header,
                                            SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kClientEndOfStream);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  if (header.payload_length != 0) {
    return absl::InternalError(
        "Expected zero payload length on ClientEndOfStream");
  }
  stream_id = header.stream_id;
  return absl::OkStatus();
}

void ClientEndOfStream::Serialize(const SerializeContext& ctx,
                                  BufferPair* out) const {
  AddInlineFrame(
      FrameType::kClientEndOfStream, stream_id, [](SliceBuffer&) {}, out);
}

std::string ClientEndOfStream::ToString() const { return "ClientEndOfStream"; }

absl::Status MessageFrame::Deserialize(const DeserializeContext& ctx,
                                       const FrameHeader& header,
                                       SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kMessage);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  message = Arena::MakePooled<Message>(std::move(payload), 0);
  return absl::OkStatus();
}

void MessageFrame::Serialize(const SerializeContext& ctx, BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddFrame(FrameType::kMessage, stream_id, *message->payload(), ctx.alignment, out);
}

std::string MessageFrame::ToString() const {
  std::string out = "MessageFrame{";
  if (message.get() != nullptr) {
    absl::StrAppend(&out, ", message=", message->DebugString().c_str());
  }
  absl::StrAppend(&out, "}");
  return out;
}

absl::Status ServerInitialMetadataFrame::Deserialize(const DeserializeContext& ctx,
                                                     const FrameHeader& header,
                                                     SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kServerInitialMetadata);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  return ReadMetadata(ctx.parser, std::move(payload), header.stream_id, true, false,
                      ctx.bitsrc, &headers);
}

void ServerInitialMetadataFrame::Serialize(const SerializeContext& ctx,
                                           BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddInlineFrame(
      FrameType::kServerInitialMetadata, stream_id,
      [&ctx, this](SliceBuffer& out) {
        ctx.saw_encoding_errors |= !ctx.encoder->EncodeRawHeaders(*headers, out);
      },
      out);
}

std::string ServerInitialMetadataFrame::ToString() const {
  return absl::StrCat(
      "ServerInitialMetadataFrame{stream_id=", stream_id,
      ", headers=", headers == nullptr ? "" : headers->DebugString(), "}");
}

absl::Status ServerTrailingMetadataFrame::Deserialize(const DeserializeContext& ctx,
                                                      const FrameHeader& header,
                                                      SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kServerTrailingMetadata);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  return ReadMetadata(ctx.parser, std::move(payload), header.stream_id, false, false,
                      ctx.bitsrc, &trailers);
}

void ServerTrailingMetadataFrame::Serialize(const SerializeContext& ctx,
                                            BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddInlineFrame(
      FrameType::kServerTrailingMetadata, stream_id,
      [&ctx, this](SliceBuffer& out) {
        ctx.saw_encoding_errors |= !ctx.encoder->EncodeRawHeaders(*trailers, out);
      },
      out);
}

std::string ServerTrailingMetadataFrame::ToString() const {
  return absl::StrCat(
      "ServerTrailingMetadataFrame{stream_id=", stream_id,
      ", trailers=", trailers == nullptr ? "" : trailers->DebugString(), "}");
}

absl::Status CancelFrame::Deserialize(const DeserializeContext& ctx,
                                      const FrameHeader& header,
                                      SliceBuffer payload) {
  // Ensure the frame type is Cancel
  CHECK_EQ(header.type, FrameType::kCancel);
  
  // Ensure the stream_id is non-zero
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  
  // Ensure there is no payload
  if (payload.Length() != 0) {
    return absl::InternalError("Unexpected payload for Cancel frame");
  }
  
  // Set the stream_id
  stream_id = header.stream_id;
  
  return absl::OkStatus();
}

void CancelFrame::Serialize(const SerializeContext& ctx,
                            BufferPair* out) const {
  // Ensure the stream_id is non-zero
  CHECK_NE(stream_id, 0u);
  
  // Create a FrameHeader for the Cancel frame
  FrameHeader header;
  header.type = FrameType::kCancel;
  header.stream_id = stream_id;
  
  // Serialize the header into the output buffer
  header.Serialize(out->control.AddTiny(FrameHeader::kFrameHeaderSize));
}

std::string CancelFrame::ToString() const {
  return absl::StrCat("CancelFrame{stream_id=", stream_id, "}");
}

}  // namespace chaotic_good
}  // namespace grpc_core
