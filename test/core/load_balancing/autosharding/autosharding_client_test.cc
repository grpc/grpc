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
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

using ::google::cloud::autosharding::v1::WatchShardingAssignmentRequest;
using ::google::cloud::autosharding::v1::WatchShardingAssignmentResponse;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::Optional;

constexpr absl::string_view kTarget = "my_autosharding_target";
constexpr absl::string_view kClientUuid = "my_client_uuid";
constexpr absl::string_view kStreamFailedError =
    "stream to autosharding server failed: UNAVAILABLE: stream failed";

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

struct TestSlice {
  absl::string_view start_key;
  // Not set for +infinity.
  std::optional<absl::string_view> end_key;
  std::vector<int32_t> endpoint_indices;
};

WatchShardingAssignmentResponse MakeChunk(
    const std::vector<TestSlice>& slices,
    const std::vector<absl::string_view>& endpoint_names) {
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
  for (absl::string_view name : endpoint_names) {
    chunk->add_endpoints()->set_endpoint(name);
  }
  return response;
}

WatchShardingAssignmentResponse MakeMetadata(int64_t generation) {
  WatchShardingAssignmentResponse response;
  response.mutable_metadata()->set_generation(generation);
  return response;
}

// Matches a std::optional<std::string> that holds a serialized
// InitialClientConfig request.
MATCHER_P(IsInitialClientConfig, latest_generation, "") {
  if (!arg.has_value()) {
    *result_listener << "no request";
    return false;
  }
  WatchShardingAssignmentRequest request;
  if (!request.ParseFromString(*arg)) {
    *result_listener << "cannot parse request";
    return false;
  }
  *result_listener << "request: " << request.ShortDebugString();
  return request.has_initial_client_config() &&
         request.initial_client_config().target() == kTarget &&
         request.initial_client_config().client_uuid() == kClientUuid &&
         request.initial_client_config().latest_generation() ==
             latest_generation;
}

// Matches a std::optional<std::string> that holds a serialized AssignmentAck
// request.
MATCHER_P3(IsAck, generation, accepted, error_message, "") {
  if (!arg.has_value()) {
    *result_listener << "no request";
    return false;
  }
  WatchShardingAssignmentRequest request;
  if (!request.ParseFromString(*arg)) {
    *result_listener << "cannot parse request";
    return false;
  }
  *result_listener << "request: " << request.ShortDebugString();
  return request.has_assignment_ack() &&
         request.assignment_ack().generation() == generation &&
         request.assignment_ack().accepted() == accepted &&
         request.assignment_ack().error_message() == error_message;
}

// Matches an absl::StatusOr<> that holds a value that matches value_matcher.
MATCHER_P(IsOkAndHolds, value_matcher, "") {
  if (!arg.ok()) {
    *result_listener << "status is " << arg.status();
    return false;
  }
  return ::testing::ExplainMatchResult(value_matcher, *arg, result_listener);
}

// Matches an absl::StatusOr<> whose status is equal to status.
MATCHER_P(HasStatus, status, "") {
  *result_listener << "status is " << arg.status();
  return arg.status() == status;
}

// Matches a RefCountedPtr<const AutoshardingAssignment>.
MATCHER_P2(IsAssignment, slices_matcher, endpoint_names_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "assignment is null";
    return false;
  }
  return ::testing::ExplainMatchResult(slices_matcher, arg->slices,
                                       result_listener) &&
         ::testing::ExplainMatchResult(endpoint_names_matcher,
                                       arg->endpoint_names, result_listener);
}

using Result = absl::StatusOr<RefCountedPtr<const AutoshardingAssignment>>;

std::string ResultToString(const Result& result) {
  if (!result.ok()) return result.status().ToString();
  if (*result == nullptr) return "null assignment";
  std::vector<std::string> slices;
  for (const AutoshardingAssignment::Slice& slice : (*result)->slices) {
    slices.push_back(absl::StrCat(
        "{end_key=\"", slice.end_key, "\" endpoints=[",
        absl::StrJoin(slice.endpoints, ","), "] fallback_disabled=",
        slice.fallback_disabled ? "true" : "false", "}"));
  }
  return absl::StrCat("slices=[", absl::StrJoin(slices, ", "),
                      "] endpoint_names=[",
                      absl::StrJoin((*result)->endpoint_names, ","), "]");
}

class AutoshardingClientTest : public ::testing::Test {
 protected:
  using Stream = FakeXdsTransportFactory::FakeStreamingCall;

  void SetUp() override {
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        []() { FAIL() << "Multiple concurrent reads"; }, event_engine_);
  }

  void TearDown() override {
    client_.reset();
    transport_.reset();
    transport_factory_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
    EXPECT_FALSE(result_.has_value())
        << "result queue not drained at end of test: "
        << ResultToString(*result_);
  }

  void CreateClient(Duration initial_assignment_timeout = Duration::Hours(1)) {
    absl::Status status;
    transport_ = transport_factory_->GetTransport(server_target_, &status);
    ASSERT_TRUE(status.ok()) << status;
    client_ = MakeOrphanable<AutoshardingClient>(
        transport_, std::string(kTarget), std::string(kClientUuid),
        initial_assignment_timeout, event_engine_,
        [this](RefCountedPtr<AutoshardingClient> /*client*/, Result result) {
          ASSERT_FALSE(result_.has_value())
              << "result generated before test processed previous result; "
                 "dropping new result: "
              << ResultToString(result);
          result_ = std::move(result);
        });
  }

  // Records the delay of each EventEngine::RunAfter() call.  Each delay must
  // be taken with TakeRunAfterDelay() before the next RunAfter() call.
  void RecordRunAfterDelays() {
    event_engine_->SetRunAfterDurationCallback(
        [this](EventEngine::Duration delay) {
          ASSERT_FALSE(run_after_delay_.has_value())
              << "RunAfter() called before test processed previous delay; "
                 "dropping new delay: "
              << Duration::NanosecondsRoundDown(
                     std::chrono::nanoseconds(delay).count());
          run_after_delay_ = delay;
        });
  }

  std::optional<EventEngine::Duration> TakeRunAfterDelay() {
    return std::exchange(run_after_delay_, std::nullopt);
  }

  RefCountedPtr<Stream> WaitForStream() {
    return transport_factory_->WaitForStream(server_target_,
                                             AutoshardingClient::kMethod);
  }

  static void Send(Stream* stream,
                   const WatchShardingAssignmentResponse& response) {
    stream->SendMessageToClient(response.SerializeAsString());
  }

  // Ticks until the client reports a result or the EventEngine is idle.
  // Returns std::nullopt if no result was reported.
  std::optional<Result> WaitForResult() {
    while (!result_.has_value() && !event_engine_->IsIdle()) {
      event_engine_->Tick();
    }
    return std::exchange(result_, std::nullopt);
  }

  std::shared_ptr<FuzzingEventEngine> event_engine_;
  FakeServerTarget server_target_;
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<XdsTransport> transport_;
  OrphanablePtr<AutoshardingClient> client_;
  std::optional<Result> result_;
  std::optional<EventEngine::Duration> run_after_delay_;
};

TEST_F(AutoshardingClientTest, SendsInitialClientConfig) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_TRUE(stream->wait_for_ready());
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
}

TEST_F(AutoshardingClientTest, ReportsValidAssignmentAndSendsAck) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", "m", {0}}}, {"host0"}));
  Send(stream.get(), MakeChunk({{"m", std::nullopt, {1}}}, {"host1"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("m", ElementsAre(0), false),
                              FieldsAre("", ElementsAre(1), false)),
                  ElementsAre("host0", "host1")))));
}

TEST_F(AutoshardingClientTest, AckIncludesIssuesForPartlyValidAssignment) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"m", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(),
              IsAck(1, true, "encountered 1 gap(s) in the assignment"));
  // The gap before "m" is filled with a slice that has no endpoints.
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("m", IsEmpty(), false),
                              FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
}

TEST_F(AutoshardingClientTest, RejectedAssignmentReportsError) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"b", "a", {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(),
              IsAck(1, false,
                    "no valid slice assignments in generation 1: 1 slice(s) "
                    "with start_key >= end_key"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(
                  "autosharding server sent an invalid assignment: no valid "
                  "slice assignments in generation 1: 1 slice(s) with "
                  "start_key >= end_key"))));
}

TEST_F(AutoshardingClientTest, StaleGenerationIsNacked) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // After an accepted assignment, a stale generation is only NACKed.
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(2));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(2, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(2));
  EXPECT_THAT(stream->WaitForMessageFromClient(),
              IsAck(2, false,
                    "received generation 2 is not greater than the latest "
                    "generation 2"));
  EXPECT_FALSE(WaitForResult().has_value());
}

TEST_F(AutoshardingClientTest,
       StaleGenerationBeforeFirstAssignmentIsNotReported) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // Before the first valid assignment, generation 0 is stale.  It is NACKed,
  // but it is not reported, so it does not end the initial wait.
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(0));
  EXPECT_THAT(stream->WaitForMessageFromClient(),
              IsAck(0, false,
                    "received generation 0 is not greater than the latest "
                    "generation 0"));
  // The initial assignment timer is still running, so the first reported
  // result is its timeout.
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(
                  "timed out waiting for initial assignment from autosharding "
                  "server"))));
}

TEST_F(AutoshardingClientTest, MultipleAssignmentsOnOneStream) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  // A stale generation on the same stream is NACKed and not reported.
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host1"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(),
              IsAck(1, false,
                    "received generation 1 is not greater than the latest "
                    "generation 1"));
  EXPECT_FALSE(WaitForResult().has_value());
  // A newer generation on the same stream is accepted and reported.  It has
  // only its own chunks, not the chunks of the stale assignment.
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host2"}));
  Send(stream.get(), MakeMetadata(2));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(2, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host2")))));
}

TEST_F(AutoshardingClientTest, RejectedThenValidAssignmentOnOneStream) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"b", "a", {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(),
              IsAck(1, false,
                    "no valid slice assignments in generation 1: 1 slice(s) "
                    "with start_key >= end_key"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(
                  "autosharding server sent an invalid assignment: no valid "
                  "slice assignments in generation 1: 1 slice(s) with "
                  "start_key >= end_key"))));
  // The rejected generation was not stored, so generation 1 is accepted on
  // the same stream.  The chunk of the rejected assignment is not reused.
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host1"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host1")))));
}

TEST_F(AutoshardingClientTest, ErrorIsNotReportedAfterValidAssignment) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  Send(stream.get(), MakeChunk({{"b", "a", {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(2));
  EXPECT_THAT(stream->WaitForMessageFromClient(),
              IsAck(2, false,
                    "no valid slice assignments in generation 2: 1 slice(s) "
                    "with start_key >= end_key"));
  EXPECT_FALSE(WaitForResult().has_value());
}

TEST_F(AutoshardingClientTest, IgnoresLoadReportingConfig) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  WatchShardingAssignmentResponse config;
  config.mutable_config();
  Send(stream.get(), config);
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
}

TEST_F(AutoshardingClientTest, StreamFailureReportsErrorAndRestartsStream) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_TRUE(stream->IsOrphaned());
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  ASSERT_NE(stream2, stream);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream2.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream2.get(), MakeMetadata(1));
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
}

TEST_F(AutoshardingClientTest,
       RestartedStreamSendsLatestGenerationAndDropsPartialChunks) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(5));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(5, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  // Send part of the next assignment, and then fail the stream.
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host1"}));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_TRUE(stream->IsOrphaned());
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  ASSERT_NE(stream2, stream);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(5));
  // The chunk from the old stream was dropped.
  Send(stream2.get(), MakeMetadata(6));
  EXPECT_THAT(stream2->WaitForMessageFromClient(),
              IsAck(6, false,
                    "received metadata with generation 6, but no chunks were "
                    "received"));
  EXPECT_FALSE(WaitForResult().has_value());
}

TEST_F(AutoshardingClientTest, InitialAssignmentTimerReportsError) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(
                  "timed out waiting for initial assignment from autosharding "
                  "server"))));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
}

TEST_F(AutoshardingClientTest, ValidAssignmentCancelsInitialAssignmentTimer) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  // The timer was cancelled, so it does not report an error.
  EXPECT_FALSE(WaitForResult().has_value());
}

TEST_F(AutoshardingClientTest, BackoffIsResetAfterValidAssignment) {
  RecordRunAfterDelays();
  CreateClient();
  // The first RunAfter() is the initial assignment timer.
  EXPECT_EQ(TakeRunAfterDelay(), std::chrono::hours(1));
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // First failure: initial backoff of 1s, with 20% jitter.
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  EXPECT_THAT(TakeRunAfterDelay(),
              Optional(AllOf(Ge(std::chrono::milliseconds(800)),
                             Le(std::chrono::milliseconds(1200)))));
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // Second failure: backoff grows by 1.6x.
  stream2->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  EXPECT_THAT(TakeRunAfterDelay(),
              Optional(AllOf(Ge(std::chrono::milliseconds(1280)),
                             Le(std::chrono::milliseconds(1920)))));
  RefCountedPtr<Stream> stream3 = WaitForStream();
  ASSERT_NE(stream3, nullptr);
  EXPECT_THAT(stream3->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // The third stream gets a valid assignment and then fails.  The backoff is
  // reset, and the next stream starts at once, without a retry timer.
  Send(stream3.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream3.get(), MakeMetadata(1));
  EXPECT_THAT(stream3->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  stream3->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_EQ(TakeRunAfterDelay(), std::nullopt);
  RefCountedPtr<Stream> stream4 = WaitForStream();
  ASSERT_NE(stream4, nullptr);
  EXPECT_THAT(stream4->WaitForMessageFromClient(), IsInitialClientConfig(1));
  // The fourth stream fails without a valid assignment, so it uses the reset
  // initial backoff of 1s.
  stream4->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(TakeRunAfterDelay(),
              Optional(AllOf(Ge(std::chrono::milliseconds(800)),
                             Le(std::chrono::milliseconds(1200)))));
  RefCountedPtr<Stream> stream5 = WaitForStream();
  ASSERT_NE(stream5, nullptr);
  EXPECT_THAT(stream5->WaitForMessageFromClient(), IsInitialClientConfig(1));
  EXPECT_FALSE(WaitForResult().has_value());
}

TEST_F(AutoshardingClientTest, BackoffIsNotResetWithoutValidAssignment) {
  RecordRunAfterDelays();
  CreateClient();
  EXPECT_EQ(TakeRunAfterDelay(), std::chrono::hours(1));
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  EXPECT_THAT(TakeRunAfterDelay(),
              Optional(AllOf(Ge(std::chrono::milliseconds(800)),
                             Le(std::chrono::milliseconds(1200)))));
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // The second stream receives a chunk and an invalid assignment, then fails.
  // Because no valid assignment was received, the backoff grows by 1.6x.
  Send(stream2.get(), MakeChunk({{"b", "a", {0}}}, {"host0"}));
  Send(stream2.get(), MakeMetadata(1));
  EXPECT_THAT(stream2->WaitForMessageFromClient(),
              IsAck(1, false,
                    "no valid slice assignments in generation 1: 1 slice(s) "
                    "with start_key >= end_key"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(
                  "autosharding server sent an invalid assignment: no valid "
                  "slice assignments in generation 1: 1 slice(s) with "
                  "start_key >= end_key"))));
  Send(stream2.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  stream2->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  EXPECT_THAT(TakeRunAfterDelay(),
              Optional(AllOf(Ge(std::chrono::milliseconds(1280)),
                             Le(std::chrono::milliseconds(1920)))));
  RefCountedPtr<Stream> stream3 = WaitForStream();
  ASSERT_NE(stream3, nullptr);
  EXPECT_THAT(stream3->WaitForMessageFromClient(), IsInitialClientConfig(0));
}

TEST_F(AutoshardingClientTest, ResetBackoffResetsStreamBackoff) {
  RecordRunAfterDelays();
  CreateClient();
  EXPECT_EQ(TakeRunAfterDelay(), std::chrono::hours(1));
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  EXPECT_THAT(TakeRunAfterDelay(),
              Optional(AllOf(Ge(std::chrono::milliseconds(800)),
                             Le(std::chrono::milliseconds(1200)))));
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // Without a reset, the next backoff would grow by 1.6x.
  client_->ResetBackoff();
  stream2->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  EXPECT_THAT(TakeRunAfterDelay(),
              Optional(AllOf(Ge(std::chrono::milliseconds(800)),
                             Le(std::chrono::milliseconds(1200)))));
  RefCountedPtr<Stream> stream3 = WaitForStream();
  ASSERT_NE(stream3, nullptr);
  EXPECT_THAT(stream3->WaitForMessageFromClient(), IsInitialClientConfig(0));
}

TEST_F(AutoshardingClientTest, ResetBackoffRestartsStreamWithPendingRetry) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  EXPECT_TRUE(stream->IsOrphaned());
  // The retry timer is pending.  ResetBackoff() starts the stream at once.
  const auto start = event_engine_->Now();
  client_->ResetBackoff();
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_EQ(event_engine_->Now(), start);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // The cancelled retry timer does not start another stream.
  event_engine_->TickUntilIdle();
  EXPECT_FALSE(stream2->IsOrphaned());
}

TEST_F(AutoshardingClientTest, ReadsDoNotWaitForSends) {
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // InitialClientConfig is not sent yet, but the client still reads and
  // reports assignments.
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(2));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  // The ACKs wait for the in-flight send.
  EXPECT_EQ(stream->WaitForMessageFromClient(), std::nullopt);
  // When the send completes, only the latest ACK is sent.
  stream->CompleteSendMessageFromClient();
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(2, true, ""));
  stream->CompleteSendMessageFromClient();
  EXPECT_EQ(stream->WaitForMessageFromClient(), std::nullopt);
}

TEST_F(AutoshardingClientTest, FailedSendDropsPendingAck) {
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  // The stream is ending, so the pending ACK is not sent.
  stream->CompleteSendMessageFromClient(/*ok=*/false);
  EXPECT_EQ(stream->WaitForMessageFromClient(), std::nullopt);
  // The stream saw a valid assignment, so it restarts at once.
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_TRUE(stream->IsOrphaned());
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(1));
  stream2->CompleteSendMessageFromClient();
}

TEST_F(AutoshardingClientTest, UnparseableResponseRestartsStream) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // Field 1 says 5 bytes follow, but only 2 do.
  stream->SendMessageToClient(
      absl::string_view("\x0a\x05"
                        "ab",
                        4));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(
                  "stream to autosharding server failed: INTERNAL: cannot "
                  "parse WatchShardingAssignmentResponse from autosharding "
                  "server"))));
  EXPECT_TRUE(stream->IsOrphaned());
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  ASSERT_NE(stream2, stream);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
}

TEST_F(AutoshardingClientTest, ReportsEachErrorBeforeFirstValidAssignment) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(
                  "timed out waiting for initial assignment from autosharding "
                  "server"))));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
}

TEST_F(AutoshardingClientTest, StreamErrorCancelsInitialAssignmentTimer) {
  CreateClient(/*initial_assignment_timeout=*/Duration::Seconds(10));
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
  // The first error ended the wait, so the timer does not report an error.
  EXPECT_FALSE(WaitForResult().has_value());
}

TEST_F(AutoshardingClientTest, OrphanWhileRetryTimerIsPending) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  client_.reset();
  // The retry timer was cancelled, so no new stream starts.
  EXPECT_FALSE(WaitForResult().has_value());
  EXPECT_EQ(WaitForStream(), nullptr);
}

TEST_F(AutoshardingClientTest, StaleGenerationOnRestartedStream) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(3));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(3, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_TRUE(stream->IsOrphaned());
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(3));
  Send(stream2.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream2.get(), MakeMetadata(3));
  EXPECT_THAT(stream2->WaitForMessageFromClient(),
              IsAck(3, false,
                    "received generation 3 is not greater than the latest "
                    "generation 3"));
  EXPECT_FALSE(WaitForResult().has_value());
}

TEST_F(AutoshardingClientTest, GenerationAcceptedOnRestartedStreamIsKept) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(5));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(5, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_TRUE(stream->IsOrphaned());
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(5));
  Send(stream2.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream2.get(), MakeMetadata(6));
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsAck(6, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  stream2->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_TRUE(stream2->IsOrphaned());
  RefCountedPtr<Stream> stream3 = WaitForStream();
  ASSERT_NE(stream3, nullptr);
  EXPECT_THAT(stream3->WaitForMessageFromClient(), IsInitialClientConfig(6));
}

TEST_F(AutoshardingClientTest, RejectedAssignmentDoesNotAdvanceGeneration) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeMetadata(4));
  EXPECT_THAT(stream->WaitForMessageFromClient(),
              IsAck(4, false,
                    "received metadata with generation 4, but no chunks were "
                    "received"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(
                  "autosharding server sent an invalid assignment: received "
                  "metadata with generation 4, but no chunks were received"))));
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  EXPECT_THAT(WaitForResult(),
              Optional(HasStatus(absl::UnavailableError(kStreamFailedError))));
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(0));
}

TEST_F(AutoshardingClientTest,
       UnparseableResponseAfterValidAssignmentReportsNoError) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  Send(stream.get(), MakeChunk({{"", std::nullopt, {0}}}, {"host0"}));
  Send(stream.get(), MakeMetadata(1));
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsAck(1, true, ""));
  EXPECT_THAT(WaitForResult(),
              Optional(IsOkAndHolds(IsAssignment(
                  ElementsAre(FieldsAre("", ElementsAre(0), false)),
                  ElementsAre("host0")))));
  stream->SendMessageToClient(
      absl::string_view("\x0a\x05"
                        "ab",
                        4));
  EXPECT_TRUE(stream->IsOrphaned());
  RefCountedPtr<Stream> stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  ASSERT_NE(stream2, stream);
  EXPECT_THAT(stream2->WaitForMessageFromClient(), IsInitialClientConfig(1));
  EXPECT_FALSE(WaitForResult().has_value());
}

TEST_F(AutoshardingClientTest, OrphanStopsStream) {
  CreateClient();
  RefCountedPtr<Stream> stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(stream->WaitForMessageFromClient(), IsInitialClientConfig(0));
  client_.reset();
  EXPECT_TRUE(stream->IsOrphaned());
  EXPECT_FALSE(WaitForResult().has_value());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
