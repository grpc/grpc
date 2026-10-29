//
// Copyright 2026 gRPC authors.
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

#include "src/core/load_balancing/autosharding/autosharding_client.h"

#include <grpc/grpc.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "proto/autosharding/v1/autosharding.pb.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/load_balancing/autosharding/autosharding_assignment.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/wait_for_single_owner.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

using ::google::cloud::autosharding::v1::WatchShardingAssignmentRequest;
using ::google::cloud::autosharding::v1::WatchShardingAssignmentResponse;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::testing::ElementsAre;
using ::testing::FieldsAre;

constexpr char kTarget[] = "my_autosharding_target";
constexpr char kClientUuid[] = "my_client_uuid";

class FakeServerTarget final : public XdsBootstrap::XdsServerTarget {
 public:
  const std::string& server_uri() const override { return server_uri_; }
  std::string Key() const override { return server_uri_; }
  bool Equals(const XdsServerTarget& other) const override {
    return Key() == other.Key();
  }

 private:
  std::string server_uri_ = "autosharding.example.com";
};

// Describes one SliceAssignment message.
struct TestSlice {
  std::string start_key;
  // Not set for +infinity.
  std::optional<std::string> end_key;
  std::vector<int32_t> endpoint_indices;
};

// Returns a response with one chunk.
WatchShardingAssignmentResponse MakeChunk(
    const std::vector<TestSlice>& slices,
    const std::vector<std::string>& endpoint_names) {
  WatchShardingAssignmentResponse response;
  auto* chunk = response.mutable_chunk();
  for (const TestSlice& test_slice : slices) {
    auto* slice_assignment = chunk->add_slice_assignments();
    auto* slice = slice_assignment->mutable_slice();
    slice->set_start_key(test_slice.start_key);
    if (test_slice.end_key.has_value()) slice->set_end_key(*test_slice.end_key);
    for (int32_t index : test_slice.endpoint_indices) {
      slice_assignment->add_endpoints()->set_endpoint_index(index);
    }
  }
  for (const std::string& name : endpoint_names) {
    chunk->add_endpoints()->set_endpoint(name);
  }
  return response;
}

WatchShardingAssignmentResponse MakeMetadata(int64_t generation) {
  WatchShardingAssignmentResponse response;
  response.mutable_metadata()->set_generation(generation);
  return response;
}

class AutoshardingClientTest : public ::testing::Test {
 protected:
  using Stream = FakeXdsTransportFactory::FakeStreamingCall;

  void SetUp() override {
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
    work_serializer_ = std::make_shared<WorkSerializer>(event_engine_);
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        []() { FAIL() << "Multiple concurrent reads"; }, event_engine_);
  }

  void TearDown() override {
    RunInWorkSerializer([&]() { client_.reset(); });
    transport_.reset();
    transport_factory_.reset();
    work_serializer_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  // Runs closure in the WorkSerializer and waits for it to finish.
  void RunInWorkSerializer(absl::AnyInvocable<void()> closure) {
    bool done = false;
    work_serializer_->Run([&]() {
      closure();
      done = true;
    });
    while (!done) event_engine_->Tick(std::chrono::milliseconds(1));
  }

  // Creates the transport, which uses the settings of transport_factory_ at
  // this time, and then the client.
  void CreateClient(Duration initial_assignment_timeout = Duration::Hours(1)) {
    absl::Status status;
    transport_ = transport_factory_->GetTransport(server_target_, &status);
    ASSERT_TRUE(status.ok()) << status;
    AutoshardingClient::Args args;
    args.transport = transport_;
    args.target = kTarget;
    args.client_uuid = kClientUuid;
    args.initial_assignment_timeout = initial_assignment_timeout;
    args.work_serializer = work_serializer_;
    args.event_engine = event_engine_;
    args.on_result = [this](absl::StatusOr<AutoshardingAssignment> result) {
      results_.push_back(std::move(result));
      if (orphan_client_on_result_) client_.reset();
    };
    RunInWorkSerializer([&]() {
      client_ = MakeOrphanable<AutoshardingClient>(std::move(args));
    });
  }

  RefCountedPtr<Stream> WaitForStream() {
    return transport_factory_->WaitForStream(server_target_,
                                             AutoshardingClient::kMethod);
  }

  // Fails old_stream, and waits for the client to start a new stream.
  RefCountedPtr<Stream> FailStreamAndWaitForNewStream(Stream* old_stream) {
    old_stream->MaybeSendStatusToClient(
        absl::UnavailableError("stream failed"));
    while (!old_stream->IsOrphaned()) {
      event_engine_->Tick(std::chrono::milliseconds(1));
    }
    return WaitForStream();
  }

  static std::optional<WatchShardingAssignmentRequest> ReadRequest(
      Stream* stream) {
    auto message = stream->WaitForMessageFromClient();
    if (!message.has_value()) return std::nullopt;
    WatchShardingAssignmentRequest request;
    if (!request.ParseFromString(*message)) return std::nullopt;
    return request;
  }

  // Reads the InitialClientConfig request and checks its contents.
  static void ExpectInitialClientConfig(Stream* stream,
                                        int64_t latest_generation) {
    auto request = ReadRequest(stream);
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(request->has_initial_client_config());
    EXPECT_EQ(request->initial_client_config().target(), kTarget);
    EXPECT_EQ(request->initial_client_config().client_uuid(), kClientUuid);
    EXPECT_EQ(request->initial_client_config().latest_generation(),
              latest_generation);
  }

  // Reads an AssignmentAck request and checks its contents.
  static void ExpectAck(Stream* stream, int64_t generation, bool accepted,
                        absl::string_view error_message) {
    auto request = ReadRequest(stream);
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(request->has_assignment_ack());
    EXPECT_EQ(request->assignment_ack().generation(), generation);
    EXPECT_EQ(request->assignment_ack().accepted(), accepted);
    EXPECT_EQ(request->assignment_ack().error_message(), error_message);
  }

  static void Send(Stream* stream,
                   const WatchShardingAssignmentResponse& response) {
    stream->SendMessageToClient(response.SerializeAsString());
  }

  // Sends a valid assignment with one slice that covers the keyspace.
  static void SendValidAssignment(Stream* stream, int64_t generation) {
    Send(stream, MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
    Send(stream, MakeMetadata(generation));
  }

  // Waits for the next result.
  absl::StatusOr<AutoshardingAssignment> WaitForResult() {
    while (results_.empty()) event_engine_->Tick();
    auto result = std::move(results_.front());
    results_.erase(results_.begin());
    return result;
  }

  // Runs all pending work without moving time forward much.
  void DrainWork() {
    event_engine_->TickForDuration(std::chrono::milliseconds(10));
  }

  std::shared_ptr<FuzzingEventEngine> event_engine_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  FakeServerTarget server_target_;
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<XdsTransport> transport_;
  OrphanablePtr<AutoshardingClient> client_;
  std::vector<absl::StatusOr<AutoshardingAssignment>> results_;
  // If true, the result callback orphans the client.
  bool orphan_client_on_result_ = false;
};

TEST_F(AutoshardingClientTest, SendsInitialClientConfig) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_TRUE(stream->wait_for_ready());
  ExpectInitialClientConfig(stream.get(), /*latest_generation=*/0);
}

TEST_F(AutoshardingClientTest, ReportsValidAssignmentAndSendsAck) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  Send(stream.get(), MakeChunk({{"", "m", {0}}}, {"host0"}));
  Send(stream.get(), MakeChunk({{"m", std::nullopt, {1}}}, {"host1"}));
  Send(stream.get(), MakeMetadata(1));
  ExpectAck(stream.get(), /*generation=*/1, /*accepted=*/true, "");
  auto result = WaitForResult();
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->endpoint_names, ElementsAre("host0", "host1"));
  EXPECT_THAT(result->slices,
              ElementsAre(FieldsAre("m", ElementsAre(0), false),
                          FieldsAre("", ElementsAre(1), false)));
}

TEST_F(AutoshardingClientTest, AckIncludesIssuesForPartlyValidAssignment) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  Send(stream.get(), MakeChunk({{"m", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  ExpectAck(stream.get(), 1, true, "encountered 1 gap(s) in the assignment");
  auto result = WaitForResult();
  ASSERT_TRUE(result.ok()) << result.status();
}

TEST_F(AutoshardingClientTest, RejectedAssignmentReportsError) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  Send(stream.get(), MakeChunk({{"b", "a", {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  ExpectAck(stream.get(), 1, false,
            "no valid slice assignments in generation 1: 1 slice(s) with "
            "start_key >= end_key");
  EXPECT_EQ(WaitForResult().status(),
            absl::UnavailableError(
                "autosharding server sent an invalid assignment: no valid "
                "slice assignments in generation 1: 1 slice(s) with "
                "start_key >= end_key"));
}

TEST_F(AutoshardingClientTest, StaleGenerationIsNackedAndNotReported) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  SendValidAssignment(stream.get(), 2);
  ExpectAck(stream.get(), 2, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  SendValidAssignment(stream.get(), 2);
  ExpectAck(stream.get(), 2, false,
            "received generation 2 is not greater than the latest "
            "generation 2");
  DrainWork();
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest, ErrorIsNotReportedAfterValidAssignment) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  SendValidAssignment(stream.get(), 1);
  ExpectAck(stream.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  Send(stream.get(), MakeChunk({{"b", "a", {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(2));
  ExpectAck(stream.get(), 2, false,
            "no valid slice assignments in generation 2: 1 slice(s) with "
            "start_key >= end_key");
  DrainWork();
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest, IgnoresLoadReportingConfig) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  WatchShardingAssignmentResponse config;
  config.mutable_config();
  Send(stream.get(), config);
  SendValidAssignment(stream.get(), 1);
  ExpectAck(stream.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
}

TEST_F(AutoshardingClientTest, StreamFailureReportsErrorAndRestartsStream) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  // The client restarts the stream after backoff.
  auto stream2 = FailStreamAndWaitForNewStream(stream.get());
  EXPECT_EQ(WaitForResult().status(),
            absl::UnavailableError("stream to autosharding server failed: "
                                   "UNAVAILABLE: stream failed"));
  ASSERT_NE(stream2, nullptr);
  ASSERT_NE(stream2, stream);
  ExpectInitialClientConfig(stream2.get(), 0);
  SendValidAssignment(stream2.get(), 1);
  ExpectAck(stream2.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
}

TEST_F(AutoshardingClientTest,
       RestartedStreamSendsLatestGenerationAndDropsPartialChunks) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  SendValidAssignment(stream.get(), 5);
  ExpectAck(stream.get(), 5, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  // Send part of the next assignment, and then fail the stream.
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host1"}));
  DrainWork();
  // The new stream carries the last accepted generation.
  auto stream2 = FailStreamAndWaitForNewStream(stream.get());
  ASSERT_NE(stream2, nullptr);
  ASSERT_NE(stream2, stream);
  ExpectInitialClientConfig(stream2.get(), 5);
  // The chunk from the old stream was dropped.
  Send(stream2.get(), MakeMetadata(6));
  ExpectAck(stream2.get(), 6, false,
            "received metadata with generation 6, but no chunks were "
            "received");
  // No error is reported, because a valid assignment was reported.
  DrainWork();
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest, InitialAssignmentTimerReportsError) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  EXPECT_EQ(WaitForResult().status(),
            absl::UnavailableError("timed out waiting for initial assignment "
                                   "from autosharding server"));
  // A later valid assignment is still reported.
  SendValidAssignment(stream.get(), 1);
  ExpectAck(stream.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
}

TEST_F(AutoshardingClientTest, ValidAssignmentCancelsInitialAssignmentTimer) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  SendValidAssignment(stream.get(), 1);
  ExpectAck(stream.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  event_engine_->TickForDuration(std::chrono::seconds(20));
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest, BackoffIsResetAfterValidAssignment) {
  std::vector<EventEngine::Duration> delays;
  event_engine_->SetRunAfterDurationCallback(
      [&](EventEngine::Duration delay) { delays.push_back(delay); });
  CreateClient();
  // The first RunAfter() is the initial assignment timer.
  ASSERT_THAT(delays, ElementsAre(std::chrono::hours(1)));
  delays.clear();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  // First failure: initial backoff of 1s, with 20% jitter.
  auto stream2 = FailStreamAndWaitForNewStream(stream.get());
  ASSERT_FALSE(WaitForResult().ok());
  ASSERT_NE(stream2, nullptr);
  ExpectInitialClientConfig(stream2.get(), 0);
  // Second failure: backoff grows by 1.6x.
  auto stream3 = FailStreamAndWaitForNewStream(stream2.get());
  ASSERT_FALSE(WaitForResult().ok());
  ASSERT_NE(stream3, nullptr);
  ExpectInitialClientConfig(stream3.get(), 0);
  // Third stream gets a valid assignment and then fails: backoff resets.
  SendValidAssignment(stream3.get(), 1);
  ExpectAck(stream3.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  auto stream4 = FailStreamAndWaitForNewStream(stream3.get());
  ASSERT_NE(stream4, nullptr);
  ExpectInitialClientConfig(stream4.get(), 1);
  // No error is reported after a valid assignment.
  EXPECT_TRUE(results_.empty());
  ASSERT_EQ(delays.size(), 3);
  EXPECT_GE(delays[0], std::chrono::milliseconds(800));
  EXPECT_LE(delays[0], std::chrono::milliseconds(1200));
  EXPECT_GE(delays[1], std::chrono::milliseconds(1280));
  EXPECT_LE(delays[1], std::chrono::milliseconds(1920));
  EXPECT_GE(delays[2], std::chrono::milliseconds(800));
  EXPECT_LE(delays[2], std::chrono::milliseconds(1200));
}

TEST_F(AutoshardingClientTest, EndpointIndexRefersToEarlierChunk) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  Send(stream.get(), MakeChunk({}, {"host0"}));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host1"}));
  Send(stream.get(), MakeMetadata(1));
  ExpectAck(stream.get(), 1, true, "");
  auto result = WaitForResult();
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->endpoint_names, ElementsAre("host0", "host1"));
  EXPECT_THAT(result->slices,
              ElementsAre(FieldsAre("", ElementsAre(0), false)));
}

TEST_F(AutoshardingClientTest, ReadStartsOnlyAfterSendCompletes) {
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  // InitialClientConfig is not sent yet, so the client does not read.
  SendValidAssignment(stream.get(), 1);
  DrainWork();
  EXPECT_FALSE(stream->HaveMessageFromClient());
  EXPECT_TRUE(results_.empty());
  stream->CompleteSendMessageFromClient();
  ExpectAck(stream.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  // The ack is not sent yet, so the client does not read.
  SendValidAssignment(stream.get(), 2);
  DrainWork();
  EXPECT_FALSE(stream->HaveMessageFromClient());
  EXPECT_TRUE(results_.empty());
  stream->CompleteSendMessageFromClient();
  ExpectAck(stream.get(), 2, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  stream->CompleteSendMessageFromClient();
}

TEST_F(AutoshardingClientTest, FailedSendDoesNotStartReadAndStreamRestarts) {
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  stream->CompleteSendMessageFromClient(/*ok=*/false);
  SendValidAssignment(stream.get(), 1);
  DrainWork();
  EXPECT_FALSE(stream->HaveMessageFromClient());
  EXPECT_TRUE(results_.empty());
  auto stream2 = FailStreamAndWaitForNewStream(stream.get());
  ASSERT_NE(stream2, nullptr);
  EXPECT_EQ(WaitForResult().status(),
            absl::UnavailableError("stream to autosharding server failed: "
                                   "UNAVAILABLE: stream failed"));
  ExpectInitialClientConfig(stream2.get(), 0);
  stream2->CompleteSendMessageFromClient();
}

TEST_F(AutoshardingClientTest, UnparseableResponseRestartsStream) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  // Field 1 says 5 bytes follow, but only 2 do.
  stream->SendMessageToClient(
      absl::string_view("\x0a\x05"
                        "ab",
                        4));
  EXPECT_EQ(WaitForResult().status(),
            absl::UnavailableError(
                "stream to autosharding server failed: INTERNAL: cannot parse "
                "WatchShardingAssignmentResponse from autosharding server"));
  while (!stream->IsOrphaned()) {
    event_engine_->Tick(std::chrono::milliseconds(1));
  }
  auto stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  ASSERT_NE(stream2, stream);
  ExpectInitialClientConfig(stream2.get(), 0);
}

TEST_F(AutoshardingClientTest, ReportsEachErrorBeforeFirstValidAssignment) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  EXPECT_EQ(WaitForResult().status(),
            absl::UnavailableError("timed out waiting for initial assignment "
                                   "from autosharding server"));
  auto stream2 = FailStreamAndWaitForNewStream(stream.get());
  ASSERT_NE(stream2, nullptr);
  EXPECT_EQ(WaitForResult().status(),
            absl::UnavailableError("stream to autosharding server failed: "
                                   "UNAVAILABLE: stream failed"));
  ExpectInitialClientConfig(stream2.get(), 0);
}

TEST_F(AutoshardingClientTest, ResultCallbackCanOrphanClientOnAssignment) {
  orphan_client_on_result_ = true;
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  SendValidAssignment(stream.get(), 1);
  ExpectAck(stream.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  EXPECT_EQ(client_, nullptr);
  DrainWork();
  EXPECT_TRUE(stream->IsOrphaned());
}

TEST_F(AutoshardingClientTest, ResultCallbackCanOrphanClientOnStreamFailure) {
  orphan_client_on_result_ = true;
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  ASSERT_FALSE(WaitForResult().ok());
  EXPECT_EQ(client_, nullptr);
  // The retry timer was cancelled, so no new stream starts and nothing is
  // reported.
  event_engine_->TickForDuration(std::chrono::seconds(300));
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest, StreamErrorCancelsInitialAssignmentTimer) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  auto stream2 = FailStreamAndWaitForNewStream(stream.get());
  ASSERT_NE(stream2, nullptr);
  ASSERT_FALSE(WaitForResult().ok());
  ExpectInitialClientConfig(stream2.get(), 0);
  // The first error ended the wait, so the timer does not report an error.
  event_engine_->TickForDuration(std::chrono::seconds(20));
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest, OrphanWhileRetryTimerIsPending) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  ASSERT_FALSE(WaitForResult().ok());
  RunInWorkSerializer([&]() { client_.reset(); });
  // The retry timer was cancelled, so nothing happens.
  event_engine_->TickForDuration(std::chrono::seconds(300));
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest, ResponseWithNoFieldSetIsIgnored) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  Send(stream.get(), WatchShardingAssignmentResponse());
  SendValidAssignment(stream.get(), 1);
  ExpectAck(stream.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
}

TEST_F(AutoshardingClientTest, StaleGenerationOnRestartedStream) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  SendValidAssignment(stream.get(), 3);
  ExpectAck(stream.get(), 3, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  auto stream2 = FailStreamAndWaitForNewStream(stream.get());
  ASSERT_NE(stream2, nullptr);
  ExpectInitialClientConfig(stream2.get(), 3);
  // The server resends the same generation.
  SendValidAssignment(stream2.get(), 3);
  ExpectAck(stream2.get(), 3, false,
            "received generation 3 is not greater than the latest "
            "generation 3");
  DrainWork();
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest,
       UnparseableResponseAfterValidAssignmentReportsNoError) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  SendValidAssignment(stream.get(), 1);
  ExpectAck(stream.get(), 1, true, "");
  ASSERT_TRUE(WaitForResult().ok());
  stream->SendMessageToClient(
      absl::string_view("\x0a\x05"
                        "ab",
                        4));
  while (!stream->IsOrphaned()) {
    event_engine_->Tick(std::chrono::milliseconds(1));
  }
  auto stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  ASSERT_NE(stream2, stream);
  ExpectInitialClientConfig(stream2.get(), 1);
  EXPECT_TRUE(results_.empty());
}

TEST_F(AutoshardingClientTest, OrphanStopsStream) {
  CreateClient();
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectInitialClientConfig(stream.get(), 0);
  RunInWorkSerializer([&]() { client_.reset(); });
  DrainWork();
  EXPECT_TRUE(stream->IsOrphaned());
  EXPECT_TRUE(results_.empty());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
