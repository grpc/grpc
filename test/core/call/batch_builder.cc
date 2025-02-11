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

#include "test/core/call/batch_builder.h"

#include <grpc/byte_buffer_reader.h>

#include "src/core/lib/compression/message_compress.h"

namespace grpc_core {

ByteBufferUniquePtr ByteBufferFromSlice(Slice slice) {
  return ByteBufferUniquePtr(
      grpc_raw_byte_buffer_create(const_cast<grpc_slice*>(&slice.c_slice()), 1),
      grpc_byte_buffer_destroy);
}

std::optional<std::string> FindInMetadataArray(const grpc_metadata_array& md,
                                               absl::string_view key) {
  for (size_t i = 0; i < md.count; i++) {
    if (key == StringViewFromSlice(md.metadata[i].key)) {
      return std::string(StringViewFromSlice(md.metadata[i].value));
    }
  }
  return std::nullopt;
}

std::optional<std::string> IncomingMetadata::Get(absl::string_view key) const {
  return FindInMetadataArray(*metadata_, key);
}

grpc_op IncomingMetadata::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_INITIAL_METADATA;
  op.data.recv_initial_metadata.recv_initial_metadata = metadata_.get();
  return op;
}

std::string IncomingMetadata::GetSuccessfulStateString() {
  std::string out = "incoming_metadata: {";
  for (size_t i = 0; i < metadata_->count; i++) {
    absl::StrAppend(&out, StringViewFromSlice(metadata_->metadata[i].key), ":",
                    StringViewFromSlice(metadata_->metadata[i].value), ",");
  }
  return out + "}";
}

std::string IncomingMessage::payload() const {
  Slice out;
  if (payload_->data.raw.compression > GRPC_COMPRESS_NONE) {
    grpc_slice_buffer decompressed_buffer;
    grpc_slice_buffer_init(&decompressed_buffer);
    CHECK(grpc_msg_decompress(payload_->data.raw.compression,
                              &payload_->data.raw.slice_buffer,
                              &decompressed_buffer));
    grpc_byte_buffer* rbb = grpc_raw_byte_buffer_create(
        decompressed_buffer.slices, decompressed_buffer.count);
    grpc_byte_buffer_reader reader;
    CHECK(grpc_byte_buffer_reader_init(&reader, rbb));
    out = Slice(grpc_byte_buffer_reader_readall(&reader));
    grpc_byte_buffer_reader_destroy(&reader);
    grpc_byte_buffer_destroy(rbb);
    grpc_slice_buffer_destroy(&decompressed_buffer);
  } else {
    grpc_byte_buffer_reader reader;
    CHECK(grpc_byte_buffer_reader_init(&reader, payload_));
    out = Slice(grpc_byte_buffer_reader_readall(&reader));
    grpc_byte_buffer_reader_destroy(&reader);
  }
  return std::string(out.begin(), out.end());
}

grpc_op IncomingMessage::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &payload_;
  return op;
}

std::optional<std::string> IncomingStatusOnClient::GetTrailingMetadata(
    absl::string_view key) const {
  return FindInMetadataArray(data_->trailing_metadata, key);
}

std::string IncomingStatusOnClient::GetSuccessfulStateString() {
  std::string out = absl::StrCat(
      "status_on_client: status=", data_->status,
      " msg=", data_->status_details.as_string_view(), " trailing_metadata={");
  for (size_t i = 0; i < data_->trailing_metadata.count; i++) {
    absl::StrAppend(
        &out, StringViewFromSlice(data_->trailing_metadata.metadata[i].key),
        ": ", StringViewFromSlice(data_->trailing_metadata.metadata[i].value),
        ",");
  }
  return out + "}";
}

std::string IncomingMessage::GetSuccessfulStateString() {
  if (payload_ == nullptr) return "message: empty";
  return absl::StrCat("message: ", payload().size(), "b uncompressed");
}

grpc_op IncomingStatusOnClient::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op.data.recv_status_on_client.trailing_metadata = &data_->trailing_metadata;
  op.data.recv_status_on_client.status = &data_->status;
  op.data.recv_status_on_client.status_details =
      const_cast<grpc_slice*>(&data_->status_details.c_slice());
  op.data.recv_status_on_client.error_string = &data_->error_string;
  return op;
}

grpc_op IncomingCloseOnServer::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op.data.recv_close_on_server.cancelled = &cancelled_;
  return op;
}

BatchBuilder& BatchBuilder::SendInitialMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>> md,
    uint32_t flags, std::optional<grpc_compression_level> compression_level) {
  auto& v = Make<std::vector<grpc_metadata>>();
  for (const auto& p : md) {
    grpc_metadata m;
    m.key = Make<Slice>(Slice::FromCopiedString(p.first)).c_slice();
    m.value = Make<Slice>(Slice::FromCopiedString(p.second)).c_slice();
    v.push_back(m);
  }
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_INITIAL_METADATA;
  op.flags = flags;
  op.data.send_initial_metadata.count = v.size();
  op.data.send_initial_metadata.metadata = v.data();
  if (compression_level.has_value()) {
    op.data.send_initial_metadata.maybe_compression_level.is_set = 1;
    op.data.send_initial_metadata.maybe_compression_level.level =
        compression_level.value();
  }
  ops_.push_back(op);
  return *this;
}

BatchBuilder& BatchBuilder::SendMessage(Slice payload, uint32_t flags) {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message =
      Make<ByteBufferUniquePtr>(ByteBufferFromSlice(std::move(payload))).get();
  op.flags = flags;
  ops_.push_back(op);
  return *this;
}

BatchBuilder& BatchBuilder::SendCloseFromClient() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  ops_.push_back(op);
  return *this;
}

BatchBuilder& BatchBuilder::SendStatusFromServer(
    grpc_status_code status, absl::string_view message,
    std::initializer_list<std::pair<absl::string_view, absl::string_view>> md) {
  auto& v = Make<std::vector<grpc_metadata>>();
  for (const auto& p : md) {
    grpc_metadata m;
    m.key = Make<Slice>(Slice::FromCopiedString(p.first)).c_slice();
    m.value = Make<Slice>(Slice::FromCopiedString(p.second)).c_slice();
    v.push_back(m);
  }
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op.data.send_status_from_server.trailing_metadata_count = v.size();
  op.data.send_status_from_server.trailing_metadata = v.data();
  op.data.send_status_from_server.status = status;
  op.data.send_status_from_server.status_details = &Make<grpc_slice>(
      Make<Slice>(Slice::FromCopiedString(message)).c_slice());
  ops_.push_back(op);
  return *this;
}

BatchBuilder::~BatchBuilder() {
  grpc_call_error err = grpc_call_start_batch(call_, ops_.data(), ops_.size(),
                                              CqVerifier::tag(tag_), nullptr);
  EXPECT_EQ(err, GRPC_CALL_OK) << grpc_call_error_to_string(err);
}

}  // namespace grpc_core
