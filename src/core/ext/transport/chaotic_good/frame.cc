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
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
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

struct ClientMetadataEncoder {
  void Encode(HttpPathMetadata,
              const typename HttpPathMetadata::ValueType& value) {
    out.set_path(value.as_string_view());
  }

  void Encode(HttpAuthorityMetadata,
              const typename HttpAuthorityMetadata::ValueType& value) {
    out.set_authority(value.as_string_view());
  }

  void Encode(GrpcTimeoutMetadata,
              const typename GrpcTimeoutMetadata::ValueType& value) {
    auto now = Timestamp::Now();
    if (now > value) {
      out.set_timeout_ms(0);
    } else {
      out.set_timeout_ms((value - now).millis());
    }
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    EncodeWithWarning(Slice::FromExternalString(Which::key()),
                      Slice(Which::Encode(value)));
  }

  void EncodeWithWarning(const Slice& key, const Slice& value) {
    LOG_EVERY_N_SEC(INFO, 10) << "encoding known key " << key.as_string_view()
                              << " with unknown encoding";
    Encode(key, value);
  }

  void Encode(const Slice& key, const Slice& value) {
    auto* unk = out.add_unknown_metadata();
    unk->set_key(key.as_string_view());
    unk->set_value(value.as_string_view());
  }

  chaotic_good_frame::ClientMetadata out;
};

struct ServerMetadataEncoder {
  void Encode(GrpcStatusMetadata, grpc_status_code code) {
    out.set_status(code);
  }

  void Encode(GrpcMessageMetadata, const Slice& value) {
    out.set_message(value.as_string_view());
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    EncodeWithWarning(Slice::FromExternalString(Which::key()),
                      Slice(Which::Encode(value)));
  }

  void EncodeWithWarning(const Slice& key, const Slice& value) {
    LOG_EVERY_N_SEC(INFO, 10) << "encoding known key " << key.as_string_view()
                              << " with unknown encoding";
    Encode(key, value);
  }

  void Encode(const Slice& key, const Slice& value) {
    auto* unk = out.add_unknown_metadata();
    unk->set_key(key.as_string_view());
    unk->set_value(value.as_string_view());
  }

  chaotic_good_frame::ServerMetadata out;
};

template <typename T, typename M>
absl::StatusOr<T> ReadUnknownFields(const M& msg, T md) {
  absl::Status error = absl::OkStatus();
  for (const auto& unk : msg.unknown_metadata()) {
    md->Append(unk.key(), Slice::FromCopiedString(unk.value()),
               [&error](absl::string_view error_msg, const Slice&) {
                 if (!error.ok()) return;
                 error = absl::InternalError(error_msg);
               });
  }
  if (!error.ok()) return error;
  return std::move(md);
}

}  // namespace

chaotic_good_frame::ClientMetadata ClientMetadataProtoFromGrpc(
    const ClientMetadata& md) {
  ClientMetadataEncoder e;
  md.Encode(&e);
  return std::move(e.out);
}

absl::StatusOr<ClientMetadataHandle> ClientMetadataGrpcFromProto(
    chaotic_good_frame::ClientMetadata& metadata) {
  auto md = Arena::MakePooled<ClientMetadata>();
  md->Set(GrpcStatusFromWire(), true);
  if (metadata.has_path()) {
    md->Set(HttpPathMetadata(), Slice::FromCopiedString(metadata.path()));
  }
  if (metadata.has_authority()) {
    md->Set(HttpAuthorityMetadata(),
            Slice::FromCopiedString(metadata.authority()));
  }
  if (metadata.has_timeout_ms()) {
    md->Set(GrpcTimeoutMetadata(),
            Timestamp::Now() + Duration::Milliseconds(metadata.timeout_ms()));
  }
  return ReadUnknownFields(metadata, std::move(md));
}

chaotic_good_frame::ServerMetadata ServerMetadataProtoFromGrpc(
    const ServerMetadata& md) {
  ServerMetadataEncoder e;
  md.Encode(&e);
  return std::move(e.out);
}

absl::StatusOr<ServerMetadataHandle> ServerMetadataGrpcFromProto(
    chaotic_good_frame::ServerMetadata& metadata) {
  auto md = Arena::MakePooled<ServerMetadata>();
  md->Set(GrpcStatusFromWire(), true);
  if (metadata.has_status()) {
    md->Set(GrpcStatusMetadata(),
            static_cast<grpc_status_code>(metadata.status()));
  }
  if (metadata.has_message()) {
    md->Set(GrpcMessageMetadata(), Slice::FromCopiedString(metadata.message()));
  }
  return ReadUnknownFields(metadata, std::move(md));
}

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

absl::Status ReadProto(SliceBuffer payload,
                       google::protobuf::MessageLite& msg) {
  auto payload_slice = payload.JoinIntoSlice();
  const bool ok =
      msg.ParseFromArray(payload_slice.data(), payload_slice.length());
  return ok ? absl::OkStatus() : absl::InternalError("Protobuf parse error");
}

void WriteProto(const google::protobuf::MessageLite& msg, SliceBuffer& output) {
  auto length = msg.ByteSizeLong();
  if (length <= GRPC_SLICE_INLINED_SIZE) {
    CHECK(msg.SerializeToArray(output.AddTiny(length), length));
  } else {
    auto slice = MutableSlice::CreateUninitialized(length);
    CHECK(msg.SerializeToArray(slice.data(), length));
    output.AppendIndexed(Slice(std::move(slice)));
  }
}

}  // namespace

absl::Status SettingsFrame::Deserialize(const DeserializeContext& ctx,
                                        const FrameHeader& header,
                                        SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kSettings);
  if (header.stream_id != 0) {
    return absl::InternalError("Expected stream id 0");
  }
  return ReadProto(std::move(payload), settings);
}

void SettingsFrame::Serialize(const SerializeContext& ctx,
                              BufferPair* out) const {
  AddInlineFrame(
      FrameType::kSettings, 0,
      [this](SliceBuffer& out) { WriteProto(settings, out); }, out);
}

std::string SettingsFrame::ToString() const {
  return settings.ShortDebugString();
}

absl::Status ClientInitialMetadataFrame::Deserialize(const DeserializeContext& ctx,
                                                     const FrameHeader& header,
                                                     SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kClientInitialMetadata);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  return ReadProto(std::move(payload), headers);
}

void ClientInitialMetadataFrame::Serialize(const SerializeContext& ctx,
                                           BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddInlineFrame(
      FrameType::kClientInitialMetadata, stream_id,
      [this](SliceBuffer& out) { WriteProto(headers, out); }, out);
}

std::string ClientInitialMetadataFrame::ToString() const {
  return absl::StrCat("ClientInitialMetadataFrame{stream_id=", stream_id,
                      ", headers=", headers.ShortDebugString(), "}");
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
    absl::StrAppend(&out, "message=", message->DebugString().c_str());
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
  return ReadProto(std::move(payload), headers);
}

void ServerInitialMetadataFrame::Serialize(const SerializeContext& ctx,
                                           BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddInlineFrame(
      FrameType::kServerInitialMetadata, stream_id,
      [this](SliceBuffer& out) { WriteProto(headers, out); }, out);
}

std::string ServerInitialMetadataFrame::ToString() const {
  return absl::StrCat("ServerInitialMetadataFrame{stream_id=", stream_id,
                      ", headers=", headers.ShortDebugString(), "}");
}

absl::Status ServerTrailingMetadataFrame::Deserialize(const DeserializeContext& ctx,
                                                      const FrameHeader& header,
                                                      SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kServerTrailingMetadata);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  return ReadProto(std::move(payload), trailers);
}

void ServerTrailingMetadataFrame::Serialize(const SerializeContext& ctx,
                                            BufferPair* out) const {
  CHECK_NE(stream_id, 0u);
  AddInlineFrame(
      FrameType::kServerTrailingMetadata, stream_id,
      [this](SliceBuffer& out) { WriteProto(trailers, out); }, out);
}

std::string ServerTrailingMetadataFrame::ToString() const {
  return absl::StrCat("ServerTrailingMetadataFrame{stream_id=", stream_id,
                      ", trailers=", trailers.ShortDebugString(), "}");
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
