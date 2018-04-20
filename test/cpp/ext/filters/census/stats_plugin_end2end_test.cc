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
#include "include/grpc++/grpc++.h"
#include "opencensus/stats/stats.h"
#include "opencensus/stats/testing/test_utils.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "test/cpp/ext/filters/census/echo.grpc.pb.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

using ::opencensus::stats::Aggregation;
using ::opencensus::stats::Distribution;
using ::opencensus::stats::View;
using ::opencensus::stats::ViewDescriptor;
using ::opencensus::stats::testing::TestUtils;

class EchoServer final : public EchoService::Service {
  ::grpc::Status Echo(::grpc::ServerContext* context,
                      const EchoRequest* request,
                      EchoResponse* response) override {
    if (request->status_code() == 0) {
      response->set_message(request->message());
      return ::grpc::Status::OK;
    } else {
      return ::grpc::Status(
          static_cast<::grpc::StatusCode>(request->status_code()), "");
    }
  }
};

class StatsPluginEnd2EndTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() { grpc_census_init(); }

  void SetUp() {
    // Set up a synchronous server on a different thread to avoid the asynch
    // interface.
    ::grpc::ServerBuilder builder;
    int port;
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort("0.0.0.0:0", ::grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);
    ASSERT_NE(0, port);
    server_address_ = absl::StrCat("0.0.0.0:", port);
    server_thread_ = std::thread(&StatsPluginEnd2EndTest::RunServerLoop, this);

    stub_ = EchoService::NewStub(::grpc::CreateChannel(
        server_address_, ::grpc::InsecureChannelCredentials()));
  }

  void TearDown() {
    server_->Shutdown();
    server_thread_.join();
  }

  void RunServerLoop() { server_->Wait(); }

  const std::string client_method_name_ = "grpc.testing.EchoService/Echo";
  const std::string server_method_name_ = "grpc.testing.EchoService/Echo";

  std::string server_address_;
  EchoServer service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;

  std::unique_ptr<EchoService::Stub> stub_;
};

TEST_F(StatsPluginEnd2EndTest, ErrorCount) {
  const auto client_method_descriptor =
      ViewDescriptor()
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_name("client_method")
          .set_aggregation(Aggregation::Count())
          .add_column(ClientMethodTagKey());
  View client_method_view(client_method_descriptor);
  const auto server_method_descriptor =
      ViewDescriptor()
          .set_measure(kRpcServerServerLatencyMeasureName)
          .set_name("server_method")
          .set_aggregation(Aggregation::Count())
          .add_column(ServerMethodTagKey());
  View server_method_view(server_method_descriptor);

  const auto client_status_descriptor =
      ViewDescriptor()
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_name("client_status")
          .set_aggregation(Aggregation::Count())
          .add_column(ClientStatusTagKey());
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
    request.set_status_code(i);
    EchoResponse response;
    ::grpc::ClientContext context;
    ::grpc::Status status = stub_->Echo(&context, request, &response);
  }
  absl::SleepFor(absl::Milliseconds(500));
  TestUtils::Flush();

  EXPECT_THAT(client_method_view.GetData().int_data(),
              ::testing::UnorderedElementsAre(::testing::Pair(
                  ::testing::ElementsAre(client_method_name_), 17)));
  EXPECT_THAT(server_method_view.GetData().int_data(),
              ::testing::UnorderedElementsAre(::testing::Pair(
                  ::testing::ElementsAre(server_method_name_), 17)));

  auto codes = {
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
              ::testing::UnorderedElementsAreArray(codes));
  EXPECT_THAT(server_status_view.GetData().int_data(),
              ::testing::UnorderedElementsAreArray(codes));
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
    ::grpc::ClientContext context;
    ::grpc::Status status = stub_->Echo(&context, request, &response);
    ASSERT_TRUE(status.ok());
    EXPECT_EQ("foo", response.message());
  }
  absl::SleepFor(absl::Milliseconds(500));
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
    ::grpc::ClientContext context;
    ::grpc::Status status = stub_->Echo(&context, request, &response);
    ASSERT_TRUE(status.ok());
    EXPECT_EQ("foo", response.message());
  }
  // We do not know exact latency/elapsed time, but we know it is less than the
  // entire time spent making the RPC.
  const double max_time = absl::ToDoubleMilliseconds(absl::Now() - start_time);

  absl::SleepFor(absl::Milliseconds(500));
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

TEST_F(StatsPluginEnd2EndTest, CompletedRpcs) {
  View client_completed_rpcs_view(ClientCompletedRpcsCumulative());
  View server_completed_rpcs_view(ServerCompletedRpcsCumulative());

  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  const int count = 5;
  for (int i = 0; i < count; ++i) {
    {
      ::grpc::ClientContext context;
      ::grpc::Status status = stub_->Echo(&context, request, &response);
      ASSERT_TRUE(status.ok());
      EXPECT_EQ("foo", response.message());
    }
    absl::SleepFor(absl::Milliseconds(500));
    TestUtils::Flush();

    EXPECT_THAT(client_completed_rpcs_view.GetData().int_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(client_method_name_, "OK"), i + 1)));
    EXPECT_THAT(server_completed_rpcs_view.GetData().int_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(server_method_name_, "OK"), i + 1)));
  }
}

TEST_F(StatsPluginEnd2EndTest, RequestReceivedMessagesPerRpc) {
  // TODO: Use streaming RPCs.
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
      ::grpc::ClientContext context;
      ::grpc::Status status = stub_->Echo(&context, request, &response);
      ASSERT_TRUE(status.ok());
      EXPECT_EQ("foo", response.message());
    }
    absl::SleepFor(absl::Milliseconds(500));
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

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
