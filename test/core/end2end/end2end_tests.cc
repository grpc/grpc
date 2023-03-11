
//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "test/core/end2end/end2end_tests.h"

#include "absl/random/random.h"
#include "cq_verifier.h"
#include "end2end_tests.h"

#include "src/core/lib/gprpp/no_destruct.h"

namespace grpc_core {

namespace {
NoDestruct<absl::InsecureBitGen> g_bitgen;
}  // namespace

Slice RandomSlice(size_t length) {
  size_t i;
  static const char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  std::vector<char> output;
  output.reserve(length);
  for (i = 0; i < length; ++i) {
    output[i] = chars[rand() % static_cast<int>(sizeof(chars) - 1)];
  }
  return Slice::FromCopiedBuffer(output);
}

ByteBufferUniquePtr ByteBufferFromSlice(Slice slice) {
  return ByteBufferUniquePtr(
      grpc_raw_byte_buffer_create(const_cast<grpc_slice*>(&slice.c_slice()), 1),
      grpc_byte_buffer_destroy);
}

void CoreEnd2endTest::SetUp() {
  fixture_ = GetParam().create_fixture(ChannelArgs(), ChannelArgs());
  fixture_->InitServer(ChannelArgs());
  fixture_->InitClient(ChannelArgs());
  cq_verifier_.reset(new CqVerifier(fixture_->cq()));
}

void CoreEnd2endTest::TearDown() {
  cq_verifier_.reset();
  fixture_.reset();
}

grpc_op CoreEnd2endTest::IncomingMetadata::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_INITIAL_METADATA;
  op.data.recv_initial_metadata.recv_initial_metadata = &metadata_;
  return op;
}

Slice CoreEnd2endTest::IncomingMessage::payload() const {
  grpc_byte_buffer_reader reader;
  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, payload_));
  Slice out(grpc_byte_buffer_reader_readall(&reader));
  grpc_byte_buffer_reader_destroy(&reader);
  return out;
}

grpc_op CoreEnd2endTest::IncomingMessage::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &payload_;
  return op;
}

grpc_op CoreEnd2endTest::IncomingStatusOnClient::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op.data.recv_status_on_client.trailing_metadata = &trailing_metadata_;
  op.data.recv_status_on_client.status = &status_;
  op.data.recv_status_on_client.status_details =
      const_cast<grpc_slice*>(&status_details_.c_slice());
  op.data.recv_status_on_client.error_string = &error_string_;
  return op;
}

grpc_op CoreEnd2endTest::IncomingCloseOnServer::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op.data.recv_close_on_server.cancelled = &cancelled_;
  return op;
}

CoreEnd2endTest::BatchBuilder&
CoreEnd2endTest::BatchBuilder::SendInitialMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>> md) {
  auto v = Make<std::vector<grpc_metadata>>();
  for (const auto& p : md) {
    grpc_metadata m;
    m.key = Make<Slice>(Slice::FromCopiedString(p.first)).c_slice();
    m.value = Make<Slice>(Slice::FromCopiedString(p.second)).c_slice();
    v.push_back(m);
  }
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_INITIAL_METADATA;
  op.data.send_initial_metadata.count = v.size();
  op.data.send_initial_metadata.metadata = v.data();
  ops_.push_back(op);
  return *this;
}

CoreEnd2endTest::BatchBuilder& CoreEnd2endTest::BatchBuilder::SendMessage(
    Slice payload) {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message =
      Make<ByteBufferUniquePtr>(ByteBufferFromSlice(std::move(payload))).get();
  ops_.push_back(op);
  return *this;
}

CoreEnd2endTest::BatchBuilder&
CoreEnd2endTest::BatchBuilder::SendCloseFromClient() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  ops_.push_back(op);
  return *this;
}

CoreEnd2endTest::BatchBuilder&
CoreEnd2endTest::BatchBuilder::SendStatusFromServer(
    grpc_status_code status, absl::string_view message,
    std::initializer_list<std::pair<absl::string_view, absl::string_view>> md) {
  auto v = Make<std::vector<grpc_metadata>>();
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

CoreEnd2endTest::BatchBuilder::~BatchBuilder() {
  grpc_call_error err = grpc_call_start_batch(call_, ops_.data(), ops_.size(),
                                              CqVerifier::tag(tag_), nullptr);
  EXPECT_EQ(err, GRPC_CALL_OK);
}

CoreEnd2endTest::Call CoreEnd2endTest::ClientCallBuilder::Create() {
  absl::optional<Slice> host;
  if (host_.has_value()) host = Slice::FromCopiedString(*host_);
  return Call(grpc_channel_create_call(
      test_.fixture_->client(), parent_call_, propagation_mask_,
      test_.fixture_->cq(), Slice::FromCopiedString(method_).c_slice(),
      host.has_value() ? &host->c_slice() : nullptr, deadline_, nullptr));
}

CoreEnd2endTest::IncomingCall::IncomingCall(CoreEnd2endTest& test, int tag)
    : impl_(std::make_unique<Impl>()) {
  grpc_server_request_call(test.fixture_->server(), impl_->call.call_ptr(),
                           &impl_->call_details, &impl_->request_metadata,
                           test.fixture_->cq(), test.fixture_->cq(),
                           CqVerifier::tag(tag));
}

}  // namespace grpc_core
