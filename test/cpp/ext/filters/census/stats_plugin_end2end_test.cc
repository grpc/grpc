//
//
// Copyright 2018 gRPC authors.
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

#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/stats/stats.h"
#include "opencensus/stats/testing/test_utils.h"
#include "opencensus/tags/tag_map.h"
#include "opencensus/tags/with_tag_map.h"

#include <grpc++/grpc++.h>
#include <grpcpp/opencensus.h>

#include "src/cpp/ext/filters/census/context.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/open_census_call_tracer.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/ext/filters/census/library.h"

namespace grpc {
namespace testing {
namespace {

using ::opencensus::stats::Aggregation;
using ::opencensus::stats::Distribution;
using ::opencensus::stats::View;
using ::opencensus::stats::ViewDescriptor;
using ::opencensus::stats::testing::TestUtils;
using ::opencensus::tags::WithTagMap;

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
  View client_transport_latency_view(experimental::ClientTransportLatency());
  View client_api_latency_view(grpc::internal::ClientApiLatency());

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

  // client api latency should be less than max time but greater than client
  // roundtrip (attempt) latency view.
  EXPECT_THAT(
      client_api_latency_view.GetData().distribution_data(),
      ::testing::UnorderedElementsAre(::testing::Pair(
          ::testing::ElementsAre(client_method_name_, "OK"),
          ::testing::AllOf(::testing::Property(&Distribution::count, 1),
                           ::testing::Property(&Distribution::mean,
                                               ::testing::Gt(client_latency)),
                           ::testing::Property(&Distribution::mean,
                                               ::testing::Lt(max_time))))));

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
                ::testing::AllOf(
                    ::testing::Ge(50),
                    ::testing::Le(500 * grpc_test_slowdown_factor()))))));
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

std::vector<opencensus::trace::exporter::SpanData>::const_iterator
GetSpanByName(
    const std::vector<::opencensus::trace::exporter::SpanData>& recorded_spans,
    absl::string_view name) {
  return std::find_if(
      recorded_spans.begin(), recorded_spans.end(),
      [name](auto const& span_data) { return span_data.name() == name; });
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
  TestUtils::Flush();
  ::opencensus::trace::exporter::SpanExporterTestPeer::ExportForTesting();
  traces_recorder_->StopRecording();
  auto recorded_spans = traces_recorder_->GetAndClearSpans();
  // We never ended the two spans created in the scope above, so we don't
  // expect them to be exported.
  ASSERT_EQ(3, recorded_spans.size());
  auto sent_span_data =
      GetSpanByName(recorded_spans, absl::StrCat("Sent.", client_method_name_));
  ASSERT_NE(sent_span_data, recorded_spans.end());
  auto attempt_span_data = GetSpanByName(
      recorded_spans, absl::StrCat("Attempt.", client_method_name_));
  ASSERT_NE(attempt_span_data, recorded_spans.end());
  EXPECT_EQ(sent_span_data->context().span_id(),
            attempt_span_data->parent_span_id());
  auto recv_span_data =
      GetSpanByName(recorded_spans, absl::StrCat("Recv.", server_method_name_));
  ASSERT_NE(recv_span_data, recorded_spans.end());
  EXPECT_EQ(attempt_span_data->context().span_id(),
            recv_span_data->parent_span_id());
}

bool IsAnnotationPresent(
    std::vector<opencensus::trace::exporter::SpanData>::const_iterator span,
    absl::string_view annotation) {
  for (const auto& event : span->annotations().events()) {
    if (absl::StrContains(event.event().description(), annotation)) {
      return true;
    } else if (::testing::Matches(::testing::ContainsRegex(annotation))(
                   event.event().description())) {
      return true;
    }
  }
  return false;
}

// Tests that the trace annotations for when a call is removed from pending
// resolver result queue, and for when a call is removed from pending lb pick
// queue, are recorded.
TEST_F(StatsPluginEnd2EndTest,
       TestRemovePendingResolverResultAndPendingLbPickQueueAnnotations) {
  {
    // Client spans are ended when the ClientContext's destructor is invoked.
    ChannelArguments args;
    args.SetLoadBalancingPolicyName("queue_once");
    auto channel = CreateCustomChannel(server_address_,
                                       InsecureChannelCredentials(), args);
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
  TestUtils::Flush();
  ::opencensus::trace::exporter::SpanExporterTestPeer::ExportForTesting();
  traces_recorder_->StopRecording();
  auto recorded_spans = traces_recorder_->GetAndClearSpans();
  // Check presence of trace annotation for removal from channel's pending
  // resolver result queue.
  auto sent_span_data =
      GetSpanByName(recorded_spans, absl::StrCat("Sent.", client_method_name_));
  ASSERT_NE(sent_span_data, recorded_spans.end());
  EXPECT_TRUE(
      IsAnnotationPresent(sent_span_data, "Delayed name resolution complete."));
  // Check presence of trace annotation for removal from channel's pending
  // lb pick queue.
  auto attempt_span_data = GetSpanByName(
      recorded_spans, absl::StrCat("Attempt.", client_method_name_));
  ASSERT_NE(attempt_span_data, recorded_spans.end());
  EXPECT_TRUE(
      IsAnnotationPresent(attempt_span_data, "Delayed LB pick complete."));
}

// Tests that the message size trace annotations are present.
TEST_F(StatsPluginEnd2EndTest, TestMessageSizeAnnotations) {
  {
    // Client spans are ended when the ClientContext's destructor is invoked.
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
  TestUtils::Flush();
  ::opencensus::trace::exporter::SpanExporterTestPeer::ExportForTesting();
  traces_recorder_->StopRecording();
  auto recorded_spans = traces_recorder_->GetAndClearSpans();
  // Check presence of message size annotations in attempt span
  auto attempt_span_data = GetSpanByName(
      recorded_spans, absl::StrCat("Attempt.", client_method_name_));
  ASSERT_NE(attempt_span_data, recorded_spans.end());
  EXPECT_TRUE(IsAnnotationPresent(attempt_span_data, "Send message: 5 bytes"));
  EXPECT_FALSE(IsAnnotationPresent(attempt_span_data,
                                   "Send compressed message: 5 bytes"));
  EXPECT_TRUE(
      IsAnnotationPresent(attempt_span_data, "Received message: 5 bytes"));
  EXPECT_FALSE(IsAnnotationPresent(attempt_span_data,
                                   "Received decompressed message: 5 bytes"));
  // Check presence of message size annotations in server span
  auto server_span_data =
      GetSpanByName(recorded_spans, absl::StrCat("Recv.", client_method_name_));
  ASSERT_NE(attempt_span_data, recorded_spans.end());
  EXPECT_TRUE(IsAnnotationPresent(server_span_data, "Send message: 5 bytes"));
  EXPECT_FALSE(IsAnnotationPresent(attempt_span_data,
                                   "Send compressed message: 5 bytes"));
  EXPECT_TRUE(
      IsAnnotationPresent(server_span_data, "Received message: 5 bytes"));
  EXPECT_FALSE(IsAnnotationPresent(server_span_data,
                                   "Received decompressed message: 5 bytes"));
}

std::string CreateLargeMessage() {
  char str[1024];
  for (int i = 0; i < 1023; ++i) {
    str[i] = 'a';
  }
  str[1023] = '\0';
  return std::string(str);
}

// Tests that the message size with compression trace annotations are present.
TEST_F(StatsPluginEnd2EndTest, TestMessageSizeWithCompressionAnnotations) {
  {
    // Client spans are ended when the ClientContext's destructor is invoked.
    EchoRequest request;
    request.set_message(CreateLargeMessage());
    EchoResponse response;

    grpc::ClientContext context;
    context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
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
  TestUtils::Flush();
  ::opencensus::trace::exporter::SpanExporterTestPeer::ExportForTesting();
  traces_recorder_->StopRecording();
  auto recorded_spans = traces_recorder_->GetAndClearSpans();
  // Check presence of message size annotations in attempt span
  auto attempt_span_data = GetSpanByName(
      recorded_spans, absl::StrCat("Attempt.", client_method_name_));
  ASSERT_NE(attempt_span_data, recorded_spans.end());
  EXPECT_TRUE(
      IsAnnotationPresent(attempt_span_data, "Send message: 1026 bytes"));
  // We don't know what the exact compressed message size would be
  EXPECT_TRUE(
      IsAnnotationPresent(attempt_span_data, "Send compressed message:"));
  EXPECT_TRUE(IsAnnotationPresent(attempt_span_data, "Received message:"));
  EXPECT_TRUE(IsAnnotationPresent(attempt_span_data,
                                  "Received decompressed message: 1026 bytes"));
  // Check presence of message size annotations in server span
  auto server_span_data =
      GetSpanByName(recorded_spans, absl::StrCat("Recv.", client_method_name_));
  ASSERT_NE(attempt_span_data, recorded_spans.end());
  EXPECT_TRUE(
      IsAnnotationPresent(server_span_data, "Send message: 1026 bytes"));
  // We don't know what the exact compressed message size would be
  EXPECT_TRUE(
      IsAnnotationPresent(attempt_span_data, "Send compressed message:"));
  EXPECT_TRUE(IsAnnotationPresent(server_span_data, "Received message:"));
  EXPECT_TRUE(IsAnnotationPresent(server_span_data,
                                  "Received decompressed message: 1026 bytes"));
}

// Tests that the metadata size trace annotations are present.
TEST_F(StatsPluginEnd2EndTest, TestMetadataSizeAnnotations) {
  {
    // Client spans are ended when the ClientContext's destructor is invoked.
    EchoRequest request;
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
  TestUtils::Flush();
  ::opencensus::trace::exporter::SpanExporterTestPeer::ExportForTesting();
  traces_recorder_->StopRecording();
  auto recorded_spans = traces_recorder_->GetAndClearSpans();
  // Check presence of metadata size annotations in client span.
  auto sent_span_data =
      GetSpanByName(recorded_spans, absl::StrCat("Sent.", client_method_name_));
  ASSERT_NE(sent_span_data, recorded_spans.end());
  EXPECT_TRUE(IsAnnotationPresent(
      sent_span_data,
      "gRPC metadata soft_limit:[0-9]{4,5},hard_limit:[0-9]{5},:status:["
      "0-9]{1,2},content-type:[0-9]{1,2},grpc-encoding:[0-"
      "9]{1,2},grpc-accept-encoding:[0-9]{1,2},"));
  EXPECT_TRUE(IsAnnotationPresent(
      sent_span_data,
      "gRPC metadata "
      "soft_limit:[0-9]{4,5},hard_limit:[0-9]{5},grpc-status:[0-9]{1,2},grpc-"
      "server-stats-bin:[0-9]{1,2},"));
}

// Test the working of GRPC_ARG_DISABLE_OBSERVABILITY.
TEST_F(StatsPluginEnd2EndTest, TestObservabilityDisabledChannelArg) {
  {
    // Client spans are ended when the ClientContext's destructor is invoked.
    ChannelArguments args;
    args.SetInt(GRPC_ARG_ENABLE_OBSERVABILITY, 0);
    auto channel = CreateCustomChannel(server_address_,
                                       InsecureChannelCredentials(), args);
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
    traces_recorder_->StartRecording();
    grpc::Status status = stub_->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok());
  }
  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();
  ::opencensus::trace::exporter::SpanExporterTestPeer::ExportForTesting();
  traces_recorder_->StopRecording();
  auto recorded_spans = traces_recorder_->GetAndClearSpans();
  // The size might be 0 or 1, depending on whether the server-side ends up
  // getting sampled or not.
  ASSERT_LE(recorded_spans.size(), 1);
  // Make sure that the client-side traces are not collected.
  auto sent_span_data =
      GetSpanByName(recorded_spans, absl::StrCat("Sent.", client_method_name_));
  ASSERT_EQ(sent_span_data, recorded_spans.end());
  auto attempt_span_data = GetSpanByName(
      recorded_spans, absl::StrCat("Attempt.", client_method_name_));
  ASSERT_EQ(attempt_span_data, recorded_spans.end());
}

// Test the working of EnableOpenCensusStats.
TEST_F(StatsPluginEnd2EndTest, TestGlobalEnableOpenCensusStats) {
  grpc::internal::EnableOpenCensusStats(false);

  View client_started_rpcs_view(ClientStartedRpcsCumulative());
  View server_started_rpcs_view(ServerStartedRpcsCumulative());
  View client_completed_rpcs_view(ClientCompletedRpcsCumulative());
  View server_completed_rpcs_view(ServerCompletedRpcsCumulative());

  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  {
    grpc::ClientContext context;
    grpc::Status status = stub_->Echo(&context, request, &response);
    ASSERT_TRUE(status.ok());
    EXPECT_EQ("foo", response.message());
  }
  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();

  EXPECT_TRUE(client_started_rpcs_view.GetData().int_data().empty());
  EXPECT_TRUE(server_started_rpcs_view.GetData().int_data().empty());
  EXPECT_TRUE(client_completed_rpcs_view.GetData().int_data().empty());
  EXPECT_TRUE(server_completed_rpcs_view.GetData().int_data().empty());

  grpc::internal::EnableOpenCensusStats(true);
}

// Test the working of EnableOpenCensusTracing.
TEST_F(StatsPluginEnd2EndTest, TestGlobalEnableOpenCensusTracing) {
  grpc::internal::EnableOpenCensusTracing(false);

  {
    // Client spans are ended when the ClientContext's destructor is invoked.
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
    traces_recorder_->StartRecording();
    grpc::Status status = stub_->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok());
  }
  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();
  ::opencensus::trace::exporter::SpanExporterTestPeer::ExportForTesting();
  traces_recorder_->StopRecording();
  auto recorded_spans = traces_recorder_->GetAndClearSpans();
  // No span should be exported
  ASSERT_EQ(0, recorded_spans.size());

  grpc::internal::EnableOpenCensusTracing(true);
}

// This test verifies that users depending on src/cpp/ext/filters/census header
// files can continue using the non-experimental names.
TEST(StatsPluginDeclarationTest, Declarations) {
  gpr_log(GPR_INFO, "%p", ClientMethodTagKey);
  gpr_log(GPR_INFO, "%p", ClientStatusTagKey);
  gpr_log(GPR_INFO, "%p", ServerMethodTagKey);
  gpr_log(GPR_INFO, "%p", ServerStatusTagKey);

  gpr_log(GPR_INFO, "%p", kRpcClientReceivedBytesPerRpcMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcClientReceivedMessagesPerRpcMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcClientRetriesPerCallMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcClientRetryDelayPerCallMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcClientRoundtripLatencyMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcClientSentBytesPerRpcMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcClientSentMessagesPerRpcMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcClientServerLatencyMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcClientStartedRpcsMeasureName.data());
  gpr_log(GPR_INFO, "%p",
          kRpcClientTransparentRetriesPerCallMeasureName.data());

  gpr_log(GPR_INFO, "%p", kRpcServerReceivedBytesPerRpcMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcServerReceivedMessagesPerRpcMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcServerSentBytesPerRpcMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcServerSentMessagesPerRpcMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcServerServerLatencyMeasureName.data());
  gpr_log(GPR_INFO, "%p", kRpcServerStartedRpcsMeasureName.data());

  gpr_log(GPR_INFO, "%p", ClientCompletedRpcsCumulative);
  gpr_log(GPR_INFO, "%p", ClientReceivedBytesPerRpcCumulative);
  gpr_log(GPR_INFO, "%p", ClientReceivedMessagesPerRpcCumulative);
  gpr_log(GPR_INFO, "%p", ClientRetriesCumulative);
  gpr_log(GPR_INFO, "%p", ClientRetriesPerCallCumulative);
  gpr_log(GPR_INFO, "%p", ClientRetryDelayPerCallCumulative);
  gpr_log(GPR_INFO, "%p", ClientRoundtripLatencyCumulative);
  gpr_log(GPR_INFO, "%p", ClientSentBytesPerRpcCumulative);
  gpr_log(GPR_INFO, "%p", ClientSentMessagesPerRpcCumulative);
  gpr_log(GPR_INFO, "%p", ClientServerLatencyCumulative);
  gpr_log(GPR_INFO, "%p", ClientStartedRpcsCumulative);
  gpr_log(GPR_INFO, "%p", ClientTransparentRetriesCumulative);
  gpr_log(GPR_INFO, "%p", ClientTransparentRetriesPerCallCumulative);

  gpr_log(GPR_INFO, "%p", ServerCompletedRpcsCumulative);
  gpr_log(GPR_INFO, "%p", ServerReceivedBytesPerRpcCumulative);
  gpr_log(GPR_INFO, "%p", ServerReceivedMessagesPerRpcCumulative);
  gpr_log(GPR_INFO, "%p", ServerSentBytesPerRpcCumulative);
  gpr_log(GPR_INFO, "%p", ServerSentMessagesPerRpcCumulative);
  gpr_log(GPR_INFO, "%p", ServerServerLatencyCumulative);
  gpr_log(GPR_INFO, "%p", ServerStartedRpcsCumulative);

  gpr_log(GPR_INFO, "%p", ClientCompletedRpcsMinute);
  gpr_log(GPR_INFO, "%p", ClientReceivedBytesPerRpcMinute);
  gpr_log(GPR_INFO, "%p", ClientReceivedMessagesPerRpcMinute);
  gpr_log(GPR_INFO, "%p", ClientRetriesMinute);
  gpr_log(GPR_INFO, "%p", ClientRetriesPerCallMinute);
  gpr_log(GPR_INFO, "%p", ClientRetryDelayPerCallMinute);
  gpr_log(GPR_INFO, "%p", ClientRoundtripLatencyMinute);
  gpr_log(GPR_INFO, "%p", ClientSentBytesPerRpcMinute);
  gpr_log(GPR_INFO, "%p", ClientSentMessagesPerRpcMinute);
  gpr_log(GPR_INFO, "%p", ClientServerLatencyMinute);
  gpr_log(GPR_INFO, "%p", ClientStartedRpcsMinute);
  gpr_log(GPR_INFO, "%p", ClientTransparentRetriesMinute);
  gpr_log(GPR_INFO, "%p", ClientTransparentRetriesPerCallMinute);

  gpr_log(GPR_INFO, "%p", ServerCompletedRpcsMinute);
  gpr_log(GPR_INFO, "%p", ServerReceivedBytesPerRpcMinute);
  gpr_log(GPR_INFO, "%p", ServerReceivedMessagesPerRpcMinute);
  gpr_log(GPR_INFO, "%p", ServerSentBytesPerRpcMinute);
  gpr_log(GPR_INFO, "%p", ServerSentMessagesPerRpcMinute);
  gpr_log(GPR_INFO, "%p", ServerServerLatencyMinute);
  gpr_log(GPR_INFO, "%p", ServerStartedRpcsMinute);

  gpr_log(GPR_INFO, "%p", ClientCompletedRpcsHour);
  gpr_log(GPR_INFO, "%p", ClientReceivedBytesPerRpcHour);
  gpr_log(GPR_INFO, "%p", ClientReceivedMessagesPerRpcHour);
  gpr_log(GPR_INFO, "%p", ClientRetriesHour);
  gpr_log(GPR_INFO, "%p", ClientRetriesPerCallHour);
  gpr_log(GPR_INFO, "%p", ClientRetryDelayPerCallHour);
  gpr_log(GPR_INFO, "%p", ClientRoundtripLatencyHour);
  gpr_log(GPR_INFO, "%p", ClientSentBytesPerRpcHour);
  gpr_log(GPR_INFO, "%p", ClientSentMessagesPerRpcHour);
  gpr_log(GPR_INFO, "%p", ClientServerLatencyHour);
  gpr_log(GPR_INFO, "%p", ClientStartedRpcsHour);
  gpr_log(GPR_INFO, "%p", ClientTransparentRetriesHour);
  gpr_log(GPR_INFO, "%p", ClientTransparentRetriesPerCallHour);

  gpr_log(GPR_INFO, "%p", ServerCompletedRpcsHour);
  gpr_log(GPR_INFO, "%p", ServerReceivedBytesPerRpcHour);
  gpr_log(GPR_INFO, "%p", ServerReceivedMessagesPerRpcHour);
  gpr_log(GPR_INFO, "%p", ServerSentBytesPerRpcHour);
  gpr_log(GPR_INFO, "%p", ServerSentMessagesPerRpcHour);
  gpr_log(GPR_INFO, "%p", ServerServerLatencyHour);
  gpr_log(GPR_INFO, "%p", ServerStartedRpcsHour);
}

}  // namespace

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
