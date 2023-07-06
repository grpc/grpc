
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

#include <regex>

#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "test/core/end2end/end2end_tests.h"

#include <grpc/byte_buffer_reader.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>

#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "test/core/end2end/cq_verifier.h"

namespace grpc_core {

bool g_is_fuzzing_core_e2e_tests = false;

Slice RandomSlice(size_t length) {
  size_t i;
  static const char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  std::vector<char> output;
  output.resize(length);
  for (i = 0; i < length; ++i) {
    output[i] = chars[rand() % static_cast<int>(sizeof(chars) - 1)];
  }
  return Slice::FromCopiedBuffer(output);
}

Slice RandomBinarySlice(size_t length) {
  size_t i;
  std::vector<uint8_t> output;
  output.resize(length);
  for (i = 0; i < length; ++i) {
    output[i] = rand();
  }
  return Slice::FromCopiedBuffer(output);
}

ByteBufferUniquePtr ByteBufferFromSlice(Slice slice) {
  return ByteBufferUniquePtr(
      grpc_raw_byte_buffer_create(const_cast<grpc_slice*>(&slice.c_slice()), 1),
      grpc_byte_buffer_destroy);
}

namespace {
absl::optional<std::string> FindInMetadataArray(const grpc_metadata_array& md,
                                                absl::string_view key) {
  for (size_t i = 0; i < md.count; i++) {
    if (key == StringViewFromSlice(md.metadata[i].key)) {
      return std::string(StringViewFromSlice(md.metadata[i].value));
    }
  }
  return absl::nullopt;
}
}  // namespace

void CoreEnd2endTest::SetUp() {
  CoreConfiguration::Reset();
  initialized_ = false;
}

void CoreEnd2endTest::TearDown() {
  const bool do_shutdown = fixture_ != nullptr;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> ee;
// TODO(hork): locate the windows leak so we can enable end2end experiments.
#ifndef GPR_WINDOWS
  if (grpc_is_initialized()) {
    ee = grpc_event_engine::experimental::GetDefaultEventEngine();
  }
#endif
  ShutdownAndDestroyClient();
  ShutdownAndDestroyServer();
  cq_verifier_.reset();
  if (cq_ != nullptr) {
    grpc_completion_queue_shutdown(cq_);
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(5),
                                      nullptr);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cq_);
    cq_ = nullptr;
  }
  fixture_.reset();
  // Creating an EventEngine requires gRPC initialization, which the NoOp test
  // does not do. Skip the EventEngine check if unnecessary.
  if (ee != nullptr) {
    quiesce_event_engine_(std::move(ee));
  }
  if (do_shutdown) {
    grpc_shutdown_blocking();
    // This will wait until gRPC shutdown has actually happened to make sure
    // no gRPC resources (such as thread) are active. (timeout = 10s)
    if (!grpc_wait_until_shutdown(10)) {
      gpr_log(GPR_ERROR, "Timeout in waiting for gRPC shutdown");
    }
  }
  GPR_ASSERT(client_ == nullptr);
  GPR_ASSERT(server_ == nullptr);
  initialized_ = false;
}

absl::optional<std::string> CoreEnd2endTest::IncomingMetadata::Get(
    absl::string_view key) const {
  return FindInMetadataArray(*metadata_, key);
}

grpc_op CoreEnd2endTest::IncomingMetadata::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_INITIAL_METADATA;
  op.data.recv_initial_metadata.recv_initial_metadata = metadata_.get();
  return op;
}

std::string CoreEnd2endTest::IncomingMetadata::GetSuccessfulStateString() {
  std::string out = "incoming_metadata: {";
  for (size_t i = 0; i < metadata_->count; i++) {
    absl::StrAppend(&out, StringViewFromSlice(metadata_->metadata[i].key), ":",
                    StringViewFromSlice(metadata_->metadata[i].value), ",");
  }
  return out + "}";
}

std::string CoreEnd2endTest::IncomingMessage::payload() const {
  Slice out;
  if (payload_->data.raw.compression > GRPC_COMPRESS_NONE) {
    grpc_slice_buffer decompressed_buffer;
    grpc_slice_buffer_init(&decompressed_buffer);
    GPR_ASSERT(grpc_msg_decompress(payload_->data.raw.compression,
                                   &payload_->data.raw.slice_buffer,
                                   &decompressed_buffer));
    grpc_byte_buffer* rbb = grpc_raw_byte_buffer_create(
        decompressed_buffer.slices, decompressed_buffer.count);
    grpc_byte_buffer_reader reader;
    GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, rbb));
    out = Slice(grpc_byte_buffer_reader_readall(&reader));
    grpc_byte_buffer_reader_destroy(&reader);
    grpc_byte_buffer_destroy(rbb);
    grpc_slice_buffer_destroy(&decompressed_buffer);
  } else {
    grpc_byte_buffer_reader reader;
    GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, payload_));
    out = Slice(grpc_byte_buffer_reader_readall(&reader));
    grpc_byte_buffer_reader_destroy(&reader);
  }
  return std::string(out.begin(), out.end());
}

grpc_op CoreEnd2endTest::IncomingMessage::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &payload_;
  return op;
}

absl::optional<std::string>
CoreEnd2endTest::IncomingStatusOnClient::GetTrailingMetadata(
    absl::string_view key) const {
  return FindInMetadataArray(data_->trailing_metadata, key);
}

std::string
CoreEnd2endTest::IncomingStatusOnClient::GetSuccessfulStateString() {
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

std::string CoreEnd2endTest::IncomingMessage::GetSuccessfulStateString() {
  if (payload_ == nullptr) return "message: empty";
  return absl::StrCat("message: ", payload().size(), "b uncompressed");
}

grpc_op CoreEnd2endTest::IncomingStatusOnClient::MakeOp() {
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

grpc_op CoreEnd2endTest::IncomingCloseOnServer::MakeOp() {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op.data.recv_close_on_server.cancelled = &cancelled_;
  return op;
}

CoreEnd2endTest::BatchBuilder&
CoreEnd2endTest::BatchBuilder::SendInitialMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>> md,
    uint32_t flags, absl::optional<grpc_compression_level> compression_level) {
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

CoreEnd2endTest::BatchBuilder& CoreEnd2endTest::BatchBuilder::SendMessage(
    Slice payload, uint32_t flags) {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message =
      Make<ByteBufferUniquePtr>(ByteBufferFromSlice(std::move(payload))).get();
  op.flags = flags;
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

CoreEnd2endTest::BatchBuilder::~BatchBuilder() {
  grpc_call_error err = grpc_call_start_batch(call_, ops_.data(), ops_.size(),
                                              CqVerifier::tag(tag_), nullptr);
  EXPECT_EQ(err, GRPC_CALL_OK) << grpc_call_error_to_string(err);
}

CoreEnd2endTest::Call CoreEnd2endTest::ClientCallBuilder::Create() {
  if (auto* u = absl::get_if<UnregisteredCall>(&call_selector_)) {
    absl::optional<Slice> host;
    if (u->host.has_value()) host = Slice::FromCopiedString(*u->host);
    test_.ForceInitialized();
    return Call(
        grpc_channel_create_call(
            test_.client(), parent_call_, propagation_mask_, test_.cq(),
            Slice::FromCopiedString(u->method).c_slice(),
            host.has_value() ? &host->c_slice() : nullptr, deadline_, nullptr),
        &test_);
  } else {
    return Call(grpc_channel_create_registered_call(
                    test_.client(), parent_call_, propagation_mask_, test_.cq(),
                    absl::get<void*>(call_selector_), deadline_, nullptr),
                &test_);
  }
}

CoreEnd2endTest::IncomingCall::IncomingCall(CoreEnd2endTest& test, int tag)
    : impl_(std::make_unique<Impl>(&test)) {
  test.ForceInitialized();
  grpc_server_request_call(test.server(), impl_->call.call_ptr(),
                           &impl_->call_details, &impl_->request_metadata,
                           test.cq(), test.cq(), CqVerifier::tag(tag));
}

absl::optional<std::string> CoreEnd2endTest::IncomingCall::GetInitialMetadata(
    absl::string_view key) const {
  return FindInMetadataArray(impl_->request_metadata, key);
}

void CoreEnd2endTest::ForceInitialized() {
  if (!initialized_) {
    initialized_ = true;
    InitServer(ChannelArgs());
    InitClient(ChannelArgs());
  }
}

void CoreEnd2endTestRegistry::RegisterTest(absl::string_view suite,
                                           absl::string_view name,
                                           MakeTestFn make_test,
                                           SourceLocation) {
  if (absl::StartsWith(name, "DISABLED_")) return;
  auto& tests = tests_by_suite_[suite];
  GPR_ASSERT(tests.count(name) == 0);
  tests[name] = std::move(make_test);
}

void CoreEnd2endTestRegistry::RegisterSuite(
    absl::string_view suite, std::vector<const CoreTestConfiguration*> configs,
    SourceLocation) {
  GPR_ASSERT(suites_.count(suite) == 0);
  suites_[suite] = std::move(configs);
}

namespace {
template <typename Map>
std::vector<absl::string_view> KeysFrom(const Map& map) {
  std::vector<absl::string_view> out;
  out.reserve(map.size());
  for (const auto& elem : map) {
    out.push_back(elem.first);
  }
  return out;
}
}  // namespace

std::vector<CoreEnd2endTestRegistry::Test> CoreEnd2endTestRegistry::AllTests() {
  std::vector<Test> tests;
  for (const auto& suite_configs : suites_) {
    if (suite_configs.second.empty()) {
      CrashWithStdio(
          absl::StrCat("Suite ", suite_configs.first, " has no tests"));
    }
    for (const auto& test_factory : tests_by_suite_[suite_configs.first]) {
      for (const auto* config : suite_configs.second) {
        tests.push_back(Test{suite_configs.first, test_factory.first, config,
                             test_factory.second});
      }
    }
  }
  return tests;
}

}  // namespace grpc_core
