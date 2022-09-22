/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/stats/stats.h"
#include "opencensus/stats/tag_key.h"
#include "opencensus/stats/testing/test_utils.h"
#include "opencensus/tags/tag_map.h"
#include "opencensus/tags/with_tag_map.h"
#include "opencensus/trace/exporter/span_exporter.h"

#include <grpc++/grpc++.h>
#include <grpcpp/opencensus.h>

#include "src/cpp/ext/filters/census/context.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace opencensus {
namespace trace {
namespace exporter {
class SpanExporterTestPeer {
 public:
  static constexpr auto& ExportForTesting = SpanExporter::ExportForTesting;
};
}  // namespace exporter
}  // namespace trace
}  // namespace opencensus

namespace grpc {
namespace testing {
namespace {

using ::opencensus::stats::Aggregation;
using ::opencensus::stats::Distribution;
using ::opencensus::stats::View;
using ::opencensus::stats::ViewDescriptor;
using ::opencensus::stats::testing::TestUtils;
using ::opencensus::tags::TagKey;
using ::opencensus::tags::WithTagMap;

const auto TEST_TAG_KEY = TagKey::Register("my_key");
const auto TEST_TAG_VALUE = "my_value";
const char* kExpectedTraceIdKey = "expected_trace_id";

class EchoServer final : public TestServiceImpl {
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    CheckMetadata(context);
    return TestServiceImpl::Echo(context, request, response);
  }

  Status BidiStream(
      ServerContext* context,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    CheckMetadata(context);
    return TestServiceImpl::BidiStream(context, stream);
  }

 private:
  void CheckMetadata(ServerContext* context) {
    for (const auto& metadata : context->client_metadata()) {
      if (metadata.first == kExpectedTraceIdKey) {
        EXPECT_EQ(metadata.second, reinterpret_cast<const CensusContext*>(
                                       context->census_context())
                                       ->Span()
                                       .context()
                                       .trace_id()
                                       .ToHex());
        break;
      }
    }
  }
};

// A handler that records exported traces. Traces can later be retrieved and
// inspected.
class ExportedTracesRecorder
    : public ::opencensus::trace::exporter::SpanExporter::Handler {
 public:
  ExportedTracesRecorder() : is_recording_(false) {}
  void Export(const std::vector<::opencensus::trace::exporter::SpanData>& spans)
      override {
    absl::MutexLock lock(&mutex_);
    if (is_recording_) {
      for (auto const& span : spans) {
        recorded_spans_.push_back(span);
      }
    }
  }

  void StartRecording() {
    absl::MutexLock lock(&mutex_);
    ASSERT_FALSE(is_recording_);
    is_recording_ = true;
  }

  void StopRecording() {
    absl::MutexLock lock(&mutex_);
    ASSERT_TRUE(is_recording_);
    is_recording_ = false;
  }

  std::vector<::opencensus::trace::exporter::SpanData> GetAndClearSpans() {
    absl::MutexLock lock(&mutex_);
    return std::move(recorded_spans_);
  }

 private:
  // This mutex is necessary as the SpanExporter runs a loop on a separate
  // thread which periodically exports spans.
  absl::Mutex mutex_;
  bool is_recording_ ABSL_GUARDED_BY(mutex_);
  std::vector<::opencensus::trace::exporter::SpanData> recorded_spans_
      ABSL_GUARDED_BY(mutex_);
};

class StatsPluginEnd2EndTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    RegisterOpenCensusPlugin();
    // OpenCensus C++ has no API to unregister a previously-registered handler,
    // therefore we register this handler once, and enable/disable recording in
    // the individual tests.
    ::opencensus::trace::exporter::SpanExporter::RegisterHandler(
        absl::WrapUnique(traces_recorder_));
  }

  void SetUp() override {
    // Set up a synchronous server on a different thread to avoid the asynch
    // interface.
    grpc::ServerBuilder builder;
    int port;
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort("0.0.0.0:0", grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);
    ASSERT_NE(0, port);
    server_address_ = absl::StrCat("localhost:", port);
    server_thread_ = std::thread(&StatsPluginEnd2EndTest::RunServerLoop, this);

    stub_ = EchoTestService::NewStub(grpc::CreateChannel(
        server_address_, grpc::InsecureChannelCredentials()));
  }

  void ResetStub(std::shared_ptr<Channel> channel) {
    stub_ = EchoTestService::NewStub(channel);
  }

  void TearDown() override {
    server_->Shutdown();
    server_thread_.join();
  }

  void RunServerLoop() { server_->Wait(); }

  const std::string client_method_name_ = "grpc.testing.EchoTestService/Echo";
  const std::string server_method_name_ = "grpc.testing.EchoTestService/Echo";

  std::string server_address_;
  EchoServer service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;

  std::unique_ptr<EchoTestService::Stub> stub_;
  static ExportedTracesRecorder* traces_recorder_;
};

ExportedTracesRecorder* StatsPluginEnd2EndTest::traces_recorder_ =
    new ExportedTracesRecorder();

TEST_F(StatsPluginEnd2EndTest, ErrorCount) {
  const auto client_method_descriptor =
      ViewDescriptor()
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_name("client_method")
          .set_aggregation(Aggregation::Count())
          .add_column(ClientMethodTagKey())
          .add_column(TEST_TAG_KEY);
  View client_method_view(client_method_descriptor);
  const auto server_method_descriptor =
      ViewDescriptor()
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_name("server_method")
          .set_aggregation(Aggregation::Count())
          .add_column(ServerMethodTagKey());
  //.add_column(TEST_TAG_KEY);
  View server_method_view(server_method_descriptor);

  const auto client_status_descriptor =
      ViewDescriptor()
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_name("client_status")
          .set_aggregation(Aggregation::Count())
          .add_column(ClientStatusTagKey())
          .add_column(TEST_TAG_KEY);
  View client_status_view(client_status_descriptor);
  const auto server_status_descriptor =
      ViewDescriptor()
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_name("server_status")
          .set_aggregation(Aggregation::Count())
          .add_column(ServerStatusTagKey());
  View server_status_view(server_status_descriptor);

  // Cover all valid statuses.
  for (int i = 0; i <= 16; ++i) {
    EchoRequest request;
    request.set_message("foo");
    request.mutable_param()->mutable_expected_error()->set_code(i);
    EchoResponse response;
    grpc::ClientContext context;
    {
      WithTagMap tags({{TEST_TAG_KEY, TEST_TAG_VALUE}});
      grpc::Status status = stub_->Echo(&context, request, &response);
    }
  }
  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();

  // Client side views can be tagged with custom tags.
  EXPECT_THAT(
      client_method_view.GetData().int_data(),
      ::testing::UnorderedElementsAre(::testing::Pair(
          ::testing::ElementsAre(client_method_name_, TEST_TAG_VALUE), 17)));
  // TODO(unknown): Implement server view tagging with custom tags.
  EXPECT_THAT(server_method_view.GetData().int_data(),
              ::testing::UnorderedElementsAre(::testing::Pair(
                  ::testing::ElementsAre(server_method_name_), 17)));

  // Client side views can be tagged with custom tags.
  auto client_tags = {
      ::testing::Pair(::testing::ElementsAre("OK", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("CANCELLED", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("UNKNOWN", TEST_TAG_VALUE), 1),
      ::testing::Pair(
          ::testing::ElementsAre("INVALID_ARGUMENT", TEST_TAG_VALUE), 1),
      ::testing::Pair(
          ::testing::ElementsAre("DEADLINE_EXCEEDED", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("NOT_FOUND", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("ALREADY_EXISTS", TEST_TAG_VALUE),
                      1),
      ::testing::Pair(
          ::testing::ElementsAre("PERMISSION_DENIED", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("UNAUTHENTICATED", TEST_TAG_VALUE),
                      1),
      ::testing::Pair(
          ::testing::ElementsAre("RESOURCE_EXHAUSTED", TEST_TAG_VALUE), 1),
      ::testing::Pair(
          ::testing::ElementsAre("FAILED_PRECONDITION", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("ABORTED", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("OUT_OF_RANGE", TEST_TAG_VALUE),
                      1),
      ::testing::Pair(::testing::ElementsAre("UNIMPLEMENTED", TEST_TAG_VALUE),
                      1),
      ::testing::Pair(::testing::ElementsAre("INTERNAL", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("UNAVAILABLE", TEST_TAG_VALUE), 1),
      ::testing::Pair(::testing::ElementsAre("DATA_LOSS", TEST_TAG_VALUE), 1),
  };

  // TODO(unknown): Implement server view tagging with custom tags.
  auto server_tags = {
      ::testing::Pair(::testing::ElementsAre("OK"), 1),
      ::testing::Pair(::testing::ElementsAre("CANCELLED"), 1),
      ::testing::Pair(::testing::ElementsAre("UNKNOWN"), 1),
      ::testing::Pair(::testing::ElementsAre("INVALID_ARGUMENT"), 1),
      ::testing::Pair(::testing::ElementsAre("DEADLINE_EXCEEDED"), 1),
      ::testing::Pair(::testing::ElementsAre("NOT_FOUND"), 1),
      ::testing::Pair(::testing::ElementsAre("ALREADY_EXISTS"), 1),
      ::testing::Pair(::testing::ElementsAre("PERMISSION_DENIED"), 1),
      ::testing::Pair(::testing::ElementsAre("UNAUTHENTICATED"), 1),
      ::testing::Pair(::testing::ElementsAre("RESOURCE_EXHAUSTED"), 1),
      ::testing::Pair(::testing::ElementsAre("FAILED_PRECONDITION"), 1),
      ::testing::Pair(::testing::ElementsAre("ABORTED"), 1),
      ::testing::Pair(::testing::ElementsAre("OUT_OF_RANGE"), 1),
      ::testing::Pair(::testing::ElementsAre("UNIMPLEMENTED"), 1),
      ::testing::Pair(::testing::ElementsAre("INTERNAL"), 1),
      ::testing::Pair(::testing::ElementsAre("UNAVAILABLE"), 1),
      ::testing::Pair(::testing::ElementsAre("DATA_LOSS"), 1),
  };

  EXPECT_THAT(client_status_view.GetData().int_data(),
              ::testing::UnorderedElementsAreArray(client_tags));
  EXPECT_THAT(server_status_view.GetData().int_data(),
              ::testing::UnorderedElementsAreArray(server_tags));
}

TEST_F(StatsPluginEnd2EndTest, RequestReceivedBytesPerRpc) {
  View client_sent_bytes_per_rpc_view(ClientSentBytesPerRpcCumulative());
  View client_received_bytes_per_rpc_view(
      ClientReceivedBytesPerRpcCumulative());
  View server_sent_bytes_per_rpc_view(ServerSentBytesPerRpcCumulative());
  View server_received_bytes_per_rpc_view(
      ServerReceivedBytesPerRpcCumulative());

  {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->Echo(&context, request, &response);
    ASSERT_TRUE(status.ok());
    EXPECT_EQ("foo", response.message());
  }
  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();

  EXPECT_THAT(client_received_bytes_per_rpc_view.GetData().distribution_data(),
              ::testing::UnorderedElementsAre(::testing::Pair(
                  ::testing::ElementsAre(client_method_name_),
                  ::testing::AllOf(::testing::Property(&Distribution::count, 1),
                                   ::testing::Property(&Distribution::mean,
                                                       ::testing::Gt(0.0))))));
  EXPECT_THAT(client_sent_bytes_per_rpc_view.GetData().distribution_data(),
              ::testing::UnorderedElementsAre(::testing::Pair(
                  ::testing::ElementsAre(client_method_name_),
                  ::testing::AllOf(::testing::Property(&Distribution::count, 1),
                                   ::testing::Property(&Distribution::mean,
                                                       ::testing::Gt(0.0))))));
  EXPECT_THAT(server_received_bytes_per_rpc_view.GetData().distribution_data(),
              ::testing::UnorderedElementsAre(::testing::Pair(
                  ::testing::ElementsAre(server_method_name_),
                  ::testing::AllOf(::testing::Property(&Distribution::count, 1),
                                   ::testing::Property(&Distribution::mean,
                                                       ::testing::Gt(0.0))))));
  EXPECT_THAT(server_sent_bytes_per_rpc_view.GetData().distribution_data(),
              ::testing::UnorderedElementsAre(::testing::Pair(
                  ::testing::ElementsAre(server_method_name_),
                  ::testing::AllOf(::testing::Property(&Distribution::count, 1),
                                   ::testing::Property(&Distribution::mean,
                                                       ::testing::Gt(0.0))))));
}

TEST_F(StatsPluginEnd2EndTest, Latency) {
  View client_latency_view(ClientRoundtripLatencyCumulative());
  View client_server_latency_view(ClientServerLatencyCumulative());
  View server_server_latency_view(ServerServerLatencyCumulative());

  const absl::Time start_time = absl::Now();
  {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->Echo(&context, request, &response);
    ASSERT_TRUE(status.ok());
    EXPECT_EQ("foo", response.message());
  }
  // We do not know exact latency/elapsed time, but we know it is less than the
  // entire time spent making the RPC.
  const double max_time = absl::ToDoubleMilliseconds(absl::Now() - start_time);

  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();

  EXPECT_THAT(
      client_latency_view.GetData().distribution_data(),
      ::testing::UnorderedElementsAre(::testing::Pair(
          ::testing::ElementsAre(client_method_name_),
          ::testing::AllOf(
              ::testing::Property(&Distribution::count, 1),
              ::testing::Property(&Distribution::mean, ::testing::Gt(0.0)),
              ::testing::Property(&Distribution::mean,
                                  ::testing::Lt(max_time))))));

  // Elapsed time is a subinterval of total latency.
  const auto client_latency = client_latency_view.GetData()
                                  .distribution_data()
                                  .find({client_method_name_})
                                  ->second.mean();
  EXPECT_THAT(
      client_server_latency_view.GetData().distribution_data(),
      ::testing::UnorderedElementsAre(::testing::Pair(
          ::testing::ElementsAre(client_method_name_),
          ::testing::AllOf(
              ::testing::Property(&Distribution::count, 1),
              ::testing::Property(&Distribution::mean, ::testing::Gt(0.0)),
              ::testing::Property(&Distribution::mean,
                                  ::testing::Lt(client_latency))))));

  // client server elapsed time should be the same value propagated to the
  // client.
  const auto client_elapsed_time = client_server_latency_view.GetData()
                                       .distribution_data()
                                       .find({client_method_name_})
                                       ->second.mean();
  EXPECT_THAT(
      server_server_latency_view.GetData().distribution_data(),
      ::testing::UnorderedElementsAre(::testing::Pair(
          ::testing::ElementsAre(server_method_name_),
          ::testing::AllOf(
              ::testing::Property(&Distribution::count, 1),
              ::testing::Property(&Distribution::mean,
                                  ::testing::DoubleEq(client_elapsed_time))))));
}

TEST_F(StatsPluginEnd2EndTest, StartedRpcs) {
  View client_started_rpcs_view(ClientStartedRpcsCumulative());
  View server_started_rpcs_view(ServerStartedRpcsCumulative());

  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  const int count = 5;
  for (int i = 0; i < count; ++i) {
    {
      grpc::ClientContext context;
      grpc::Status status = stub_->Echo(&context, request, &response);
      ASSERT_TRUE(status.ok());
      EXPECT_EQ("foo", response.message());
    }
    absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
    TestUtils::Flush();

    EXPECT_THAT(client_started_rpcs_view.GetData().int_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(client_method_name_), i + 1)));
    EXPECT_THAT(server_started_rpcs_view.GetData().int_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(server_method_name_), i + 1)));
  }

  // Client should see started calls that are not yet completed.
  {
    ClientContext ctx;
    auto stream = stub_->BidiStream(&ctx);
    absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
    TestUtils::Flush();
    EXPECT_THAT(
        client_started_rpcs_view.GetData().int_data(),
        ::testing::Contains(::testing::Pair(
            ::testing::ElementsAre("grpc.testing.EchoTestService/BidiStream"),
            1)));
    EXPECT_THAT(
        server_started_rpcs_view.GetData().int_data(),
        ::testing::Contains(::testing::Pair(
            ::testing::ElementsAre("grpc.testing.EchoTestService/BidiStream"),
            1)));
    ctx.TryCancel();
  }
  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();
}

TEST_F(StatsPluginEnd2EndTest, CompletedRpcs) {
  View client_completed_rpcs_view(ClientCompletedRpcsCumulative());
  View server_completed_rpcs_view(ServerCompletedRpcsCumulative());

  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  const int count = 5;
  for (int i = 0; i < count; ++i) {
    {
      grpc::ClientContext context;
      grpc::Status status = stub_->Echo(&context, request, &response);
      ASSERT_TRUE(status.ok());
      EXPECT_EQ("foo", response.message());
    }
    absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
    TestUtils::Flush();

    EXPECT_THAT(client_completed_rpcs_view.GetData().int_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(client_method_name_, "OK"), i + 1)));
    EXPECT_THAT(server_completed_rpcs_view.GetData().int_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(server_method_name_, "OK"), i + 1)));
  }

  // Client should see calls that are cancelled without calling Finish().
  {
    ClientContext ctx;
    auto stream = stub_->BidiStream(&ctx);
    ctx.TryCancel();
  }
  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();
  EXPECT_THAT(client_completed_rpcs_view.GetData().int_data(),
              ::testing::Contains(::testing::Pair(
                  ::testing::ElementsAre(
                      "grpc.testing.EchoTestService/BidiStream", "CANCELLED"),
                  1)));
}

TEST_F(StatsPluginEnd2EndTest, RequestReceivedMessagesPerRpc) {
  // TODO(unknown): Use streaming RPCs.
  View client_received_messages_per_rpc_view(
      ClientSentMessagesPerRpcCumulative());
  View client_sent_messages_per_rpc_view(
      ClientReceivedMessagesPerRpcCumulative());
  View server_received_messages_per_rpc_view(
      ServerSentMessagesPerRpcCumulative());
  View server_sent_messages_per_rpc_view(
      ServerReceivedMessagesPerRpcCumulative());

  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  const int count = 5;
  for (int i = 0; i < count; ++i) {
    {
      grpc::ClientContext context;
      grpc::Status status = stub_->Echo(&context, request, &response);
      ASSERT_TRUE(status.ok());
      EXPECT_EQ("foo", response.message());
    }
    absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
    TestUtils::Flush();

    EXPECT_THAT(
        client_received_messages_per_rpc_view.GetData().distribution_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(client_method_name_),
            ::testing::AllOf(::testing::Property(&Distribution::count, i + 1),
                             ::testing::Property(&Distribution::mean,
                                                 ::testing::DoubleEq(1.0))))));
    EXPECT_THAT(
        client_sent_messages_per_rpc_view.GetData().distribution_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(client_method_name_),
            ::testing::AllOf(::testing::Property(&Distribution::count, i + 1),
                             ::testing::Property(&Distribution::mean,
                                                 ::testing::DoubleEq(1.0))))));
    EXPECT_THAT(
        server_received_messages_per_rpc_view.GetData().distribution_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(server_method_name_),
            ::testing::AllOf(::testing::Property(&Distribution::count, i + 1),
                             ::testing::Property(&Distribution::mean,
                                                 ::testing::DoubleEq(1.0))))));
    EXPECT_THAT(
        server_sent_messages_per_rpc_view.GetData().distribution_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(server_method_name_),
            ::testing::AllOf(::testing::Property(&Distribution::count, i + 1),
                             ::testing::Property(&Distribution::mean,
                                                 ::testing::DoubleEq(1.0))))));
  }
}

TEST_F(StatsPluginEnd2EndTest, TestRetryStatsWithoutAdditionalRetries) {
  View client_retries_cumulative_view(ClientRetriesCumulative());
  View client_transparent_retries_cumulative_view(
      ClientTransparentRetriesCumulative());
  View client_retry_delay_per_call_view(ClientRetryDelayPerCallCumulative());
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  const int count = 5;
  for (int i = 0; i < count; ++i) {
    {
      grpc::ClientContext context;
      grpc::Status status = stub_->Echo(&context, request, &response);
      ASSERT_TRUE(status.ok());
      EXPECT_EQ("foo", response.message());
    }
    absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
    TestUtils::Flush();
    EXPECT_THAT(
        client_retries_cumulative_view.GetData().int_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(client_method_name_), ::testing::Eq(0))));
    EXPECT_THAT(
        client_transparent_retries_cumulative_view.GetData().int_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(client_method_name_), ::testing::Eq(0))));
    EXPECT_THAT(
        client_retry_delay_per_call_view.GetData().distribution_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(client_method_name_),
            ::testing::Property(&Distribution::mean, ::testing::Eq(0)))));
  }
}

TEST_F(StatsPluginEnd2EndTest, TestRetryStatsWithAdditionalRetries) {
  View client_retries_cumulative_view(ClientRetriesCumulative());
  View client_transparent_retries_cumulative_view(
      ClientTransparentRetriesCumulative());
  View client_retry_delay_per_call_view(ClientRetryDelayPerCallCumulative());
  ChannelArguments args;
  args.SetString(GRPC_ARG_SERVICE_CONFIG,
                 "{\n"
                 "  \"methodConfig\": [ {\n"
                 "    \"name\": [\n"
                 "      { \"service\": \"grpc.testing.EchoTestService\" }\n"
                 "    ],\n"
                 "    \"retryPolicy\": {\n"
                 "      \"maxAttempts\": 3,\n"
                 "      \"initialBackoff\": \"0.1s\",\n"
                 "      \"maxBackoff\": \"120s\",\n"
                 "      \"backoffMultiplier\": 1,\n"
                 "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
                 "    }\n"
                 "  } ]\n"
                 "}");
  auto channel =
      CreateCustomChannel(server_address_, InsecureChannelCredentials(), args);
  ResetStub(channel);
  EchoRequest request;
  request.mutable_param()->mutable_expected_error()->set_code(
      StatusCode::ABORTED);
  request.set_message("foo");
  EchoResponse response;
  const int count = 5;
  for (int i = 0; i < count; ++i) {
    {
      grpc::ClientContext context;
      grpc::Status status = stub_->Echo(&context, request, &response);
      EXPECT_EQ(status.error_code(), StatusCode::ABORTED);
    }
    absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
    TestUtils::Flush();
    EXPECT_THAT(client_retries_cumulative_view.GetData().int_data(),
                ::testing::UnorderedElementsAre(
                    ::testing::Pair(::testing::ElementsAre(client_method_name_),
                                    ::testing::Eq((i + 1) * 2))));
    EXPECT_THAT(
        client_transparent_retries_cumulative_view.GetData().int_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(client_method_name_), ::testing::Eq(0))));
    auto data = client_retry_delay_per_call_view.GetData().distribution_data();
    for (const auto& entry : data) {
      gpr_log(GPR_ERROR, "Mean Retry Delay %s: %lf ms", entry.first[0].c_str(),
              entry.second.mean());
    }
    // We expect the retry delay to be around 100ms.
    EXPECT_THAT(
        client_retry_delay_per_call_view.GetData().distribution_data(),
        ::testing::UnorderedElementsAre(::testing::Pair(
            ::testing::ElementsAre(client_method_name_),
            ::testing::Property(
                &Distribution::mean,
                ::testing::AllOf(::testing::Ge(50), ::testing::Le(300))))));
  }
}

// Test that CensusContext object set by application is used.
TEST_F(StatsPluginEnd2EndTest, TestApplicationCensusContextFlows) {
  auto channel = CreateChannel(server_address_, InsecureChannelCredentials());
  ResetStub(channel);
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  grpc::CensusContext app_census_context("root", ::opencensus::tags::TagMap{});
  context.set_census_context(
      reinterpret_cast<census_context*>(&app_census_context));
  context.AddMetadata(kExpectedTraceIdKey,
                      app_census_context.Span().context().trace_id().ToHex());
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
}

TEST_F(StatsPluginEnd2EndTest, TestAllSpansAreExported) {
  {
    // Client spans are ended when the ClientContext's destructor is invoked.
    auto channel = CreateChannel(server_address_, InsecureChannelCredentials());
    ResetStub(channel);
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;

    grpc::ClientContext context;
    ::opencensus::trace::AlwaysSampler always_sampler;
    ::opencensus::trace::StartSpanOptions options;
    options.sampler = &always_sampler;
    auto sampling_span =
        ::opencensus::trace::Span::StartSpan("sampling", nullptr, options);
    grpc::CensusContext app_census_context("root", &sampling_span,
                                           ::opencensus::tags::TagMap{});
    context.set_census_context(
        reinterpret_cast<census_context*>(&app_census_context));
    context.AddMetadata(kExpectedTraceIdKey,
                        app_census_context.Span().context().trace_id().ToHex());
    traces_recorder_->StartRecording();
    grpc::Status status = stub_->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok());
  }
  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  ::opencensus::trace::exporter::SpanExporterTestPeer::ExportForTesting();
  traces_recorder_->StopRecording();
  auto recorded_spans = traces_recorder_->GetAndClearSpans();
  auto GetSpanByName = [&recorded_spans](absl::string_view name) {
    return std::find_if(
        recorded_spans.begin(), recorded_spans.end(),
        [name](auto const& span_data) { return span_data.name() == name; });
  };
  // We never ended the two spans created in the scope above, so we don't
  // expect them to be exported.
  ASSERT_EQ(3, recorded_spans.size());
  auto sent_span_data =
      GetSpanByName(absl::StrCat("Sent.", client_method_name_));
  ASSERT_NE(sent_span_data, recorded_spans.end());
  auto attempt_span_data =
      GetSpanByName(absl::StrCat("Attempt.", client_method_name_));
  ASSERT_NE(attempt_span_data, recorded_spans.end());
  EXPECT_EQ(sent_span_data->context().span_id(),
            attempt_span_data->parent_span_id());
  auto recv_span_data =
      GetSpanByName(absl::StrCat("Recv.", server_method_name_));
  ASSERT_NE(recv_span_data, recorded_spans.end());
  EXPECT_EQ(attempt_span_data->context().span_id(),
            recv_span_data->parent_span_id());
}

}  // namespace

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
