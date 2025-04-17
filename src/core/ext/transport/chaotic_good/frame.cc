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
#include "src/core/lib/promise/switch.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/bitset.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/status_helper.h"

namespace grpc_core {
namespace chaotic_good {

absl::Status ReadProto(SliceBuffer payload,
                       google::protobuf::MessageLite& msg) {
  auto payload_slice = payload.JoinIntoSlice();
  const bool ok =
      msg.ParseFromArray(payload_slice.data(), payload_slice.length());
  return ok ? absl::OkStatus() : absl::InternalError("Protobuf parse error");
}

void WriteProto(const google::protobuf::MessageLite& msg, SliceBuffer& output) {
  auto length = msg.ByteSizeLong();
  auto slice = MutableSlice::CreateUninitialized(length);
  CHECK(msg.SerializeToArray(slice.data(), length));
  output.AppendIndexed(Slice(std::move(slice)));
}

uint32_t ProtoPayloadSize(const google::protobuf::MessageLite& msg) {
  auto length = msg.ByteSizeLong();
  CHECK_LE(length, std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(length);
}

absl::Status ReadTransportProto(const FrameHeader& header, SliceBuffer payload,
                                google::protobuf::MessageLite& body) {
  if (header.stream_id != 0) {
    return absl::InternalError("Expected stream id 0");
  }
  return ReadProto(std::move(payload), body);
}

absl::Status ReadStreamProto(const FrameHeader& header, SliceBuffer payload,
                             google::protobuf::MessageLite& body,
                             uint32_t& stream_id) {
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  return ReadProto(std::move(payload), body);
}

absl::Status ReadEmptyFrame(const FrameHeader& header, uint32_t& stream_id) {
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  if (header.payload_length != 0) {
    return absl::InternalError(absl::StrCat("Expected zero payload length on ",
                                            FrameTypeString(header.type)));
  }
  stream_id = header.stream_id;
  return absl::OkStatus();
}

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
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "encoding known key " << key.as_string_view()
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

absl::Status MessageFrame::Deserialize(const FrameHeader& header,
                                       SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kMessage);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  message = Arena::MakePooled<Message>(std::move(payload), 0);
  return absl::OkStatus();
}

FrameHeader MessageFrame::MakeHeader() const {
  auto length = message->payload()->Length();
  CHECK_LE(length, std::numeric_limits<uint32_t>::max());
  return FrameHeader{FrameType::kMessage, stream_id,
                     static_cast<uint32_t>(length)};
}

void MessageFrame::SerializePayload(SliceBuffer& payload) const {
  CHECK_NE(stream_id, 0u);
  payload.Append(*message->payload());
}

std::string MessageFrame::ToString() const {
  std::string out = absl::StrCat("MessageFrame{stream_id=", stream_id);
  if (message.get() != nullptr) {
    absl::StrAppend(&out, ", message=", message->DebugString().c_str());
  }
  absl::StrAppend(&out, "}");
  return out;
}

absl::Status MessageChunkFrame::Deserialize(const FrameHeader& header,
                                            SliceBuffer payload) {
  CHECK_EQ(header.type, FrameType::kMessageChunk);
  if (header.stream_id == 0) {
    return absl::InternalError("Expected non-zero stream id");
  }
  stream_id = header.stream_id;
  this->payload = std::move(payload);
  return absl::OkStatus();
}

FrameHeader MessageChunkFrame::MakeHeader() const {
  auto length = payload.Length();
  CHECK_LE(length, std::numeric_limits<uint32_t>::max());
  return FrameHeader{FrameType::kMessageChunk, stream_id,
                     static_cast<uint32_t>(length)};
}

void MessageChunkFrame::SerializePayload(SliceBuffer& payload) const {
  CHECK_NE(stream_id, 0u);
  payload.Append(this->payload);
}

std::string MessageChunkFrame::ToString() const {
  return absl::StrCat("MessageChunk{stream_id=", stream_id,
                      ", payload=", payload.Length(), "b}");
}

namespace {
template <typename T>
absl::StatusOr<Frame> DeserializeFrame(const FrameHeader& header,
                                       SliceBuffer payload) {
  T frame;
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: Deserialize " << header << " with payload "
      << absl::CEscape(payload.JoinIntoString());
  CHECK_EQ(header.payload_length, payload.Length());
  auto s = frame.Deserialize(header, std::move(payload));
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: DeserializeFrame "
      << (s.ok() ? frame.ToString() : s.ToString());
  if (s.ok()) return std::move(frame);
  return std::move(s);
}
}  // namespace

absl::StatusOr<Frame> ParseFrame(const FrameHeader& header,
                                 SliceBuffer payload) {
  switch (header.type) {
    case FrameType::kSettings:
      return DeserializeFrame<SettingsFrame>(header, std::move(payload));
    case FrameType::kClientInitialMetadata:
      return DeserializeFrame<ClientInitialMetadataFrame>(header,
                                                          std::move(payload));
    case FrameType::kServerInitialMetadata:
      return DeserializeFrame<ServerInitialMetadataFrame>(header,
                                                          std::move(payload));
    case FrameType::kServerTrailingMetadata:
      return DeserializeFrame<ServerTrailingMetadataFrame>(header,
                                                           std::move(payload));
    case FrameType::kMessage:
      return DeserializeFrame<MessageFrame>(header, std::move(payload));
    case FrameType::kMessageChunk:
      return DeserializeFrame<MessageChunkFrame>(header, std::move(payload));
    case FrameType::kClientEndOfStream:
      return DeserializeFrame<ClientEndOfStream>(header, std::move(payload));
    case FrameType::kCancel:
      return DeserializeFrame<CancelFrame>(header, std::move(payload));
    case FrameType::kBeginMessage:
      return DeserializeFrame<BeginMessageFrame>(header, std::move(payload));
    default:
      return absl::InternalError(
          absl::StrCat("Unknown frame type: ", header.ToString(),
                       " payload:", payload.Length(), "b"));
  }
}

}  // namespace chaotic_good
}  // namespace grpc_core
