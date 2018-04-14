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
#include "src/core/ext/census/grpc_plugin.h"
#include "test/core/ext/census/echo.grpc.pb.h"
#include "test/core/util/test_config.h"

namespace opencensus {
namespace testing {
namespace {

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
  static void SetUpTestCase() { RegisterGrpcPlugin(); }

  void SetUp() {
    // Set up a synchronous server on a different thread to avoid the asynch
    // interface.
    ::grpc::ServerBuilder builder;
    int port;
    builder.AddListeningPort("[::1]:0", ::grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);
    ASSERT_NE(0, port);
    server_address_ = absl::StrCat("[::1]:", port);
    server_thread_ = std::thread(&StatsPluginEnd2EndTest::RunServerLoop, this);

    stub_ = EchoService::NewStub(::grpc::CreateChannel(
        server_address_, ::grpc::InsecureChannelCredentials()));
  }

  void TearDown() {
    server_->Shutdown();
    server_thread_.join();
  }

  void RunServerLoop() { server_->Wait(); }

  const std::string method_name_ = "/opencensus.testing.EchoService/Echo";

  std::string server_address_;
  EchoServer service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;

  std::unique_ptr<EchoService::Stub> stub_;
};

TEST_F(StatsPluginEnd2EndTest, ErrorCount) {
  const auto client_method_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientErrorCountMeasureName)
          .set_name("client_method")
          .set_aggregation(stats::Aggregation::Sum())

          .add_column(kMethodTagKey);
  stats::View client_method_view(client_method_descriptor);
  const auto server_method_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerErrorCountMeasureName)
          .set_name("server_method")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View server_method_view(client_method_descriptor);

  const auto client_status_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientErrorCountMeasureName)
          .set_name("client_status")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kStatusTagKey);
  stats::View client_status_view(client_status_descriptor);
  const auto server_status_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerErrorCountMeasureName)
          .set_name("server_status")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kStatusTagKey);
  stats::View server_status_view(server_status_descriptor);

  // Cover all valid statuses.
  for (int i = 0; i <= 16; ++i) {
    EchoRequest request;
    request.set_message("foo");
    request.set_status_code(i);
    EchoResponse response;
    ::grpc::ClientContext context;
    ::grpc::Status status = stub_->Echo(&context, request, &response);
  }

  EXPECT_THAT(client_method_view.GetData().double_data(),
              ::testing::UnorderedElementsAre(
                  ::testing::Pair(::testing::ElementsAre(method_name_), 16.0)));
  EXPECT_THAT(server_method_view.GetData().double_data(),
              ::testing::UnorderedElementsAre(
                  ::testing::Pair(::testing::ElementsAre(method_name_), 16.0)));

  auto codes = {
      ::testing::Pair(::testing::ElementsAre("OK"), 0.0),
      ::testing::Pair(::testing::ElementsAre("CANCELLED"), 1.0),
      ::testing::Pair(::testing::ElementsAre("UNKNOWN"), 1.0),
      ::testing::Pair(::testing::ElementsAre("INVALID_ARGUMENT"), 1.0),
      ::testing::Pair(::testing::ElementsAre("DEADLINE_EXCEEDED"), 1.0),
      ::testing::Pair(::testing::ElementsAre("NOT_FOUND"), 1.0),
      ::testing::Pair(::testing::ElementsAre("ALREADY_EXISTS"), 1.0),
      ::testing::Pair(::testing::ElementsAre("PERMISSION_DENIED"), 1.0),
      ::testing::Pair(::testing::ElementsAre("UNAUTHENTICATED"), 1.0),
      ::testing::Pair(::testing::ElementsAre("RESOURCE_EXHAUSTED"), 1.0),
      ::testing::Pair(::testing::ElementsAre("FAILED_PRECONDITION"), 1.0),
      ::testing::Pair(::testing::ElementsAre("ABORTED"), 1.0),
      ::testing::Pair(::testing::ElementsAre("OUT_OF_RANGE"), 1.0),
      ::testing::Pair(::testing::ElementsAre("UNIMPLEMENTED"), 1.0),
      ::testing::Pair(::testing::ElementsAre("INTERNAL"), 1.0),
      ::testing::Pair(::testing::ElementsAre("UNAVAILABLE"), 1.0),
      ::testing::Pair(::testing::ElementsAre("DATA_LOSS"), 1.0),
  };

  EXPECT_THAT(client_status_view.GetData().double_data(),
              ::testing::UnorderedElementsAreArray(codes));
  EXPECT_THAT(server_status_view.GetData().double_data(),
              ::testing::UnorderedElementsAreArray(codes));
}

TEST_F(StatsPluginEnd2EndTest, RequestResponseBytes) {
  const auto client_request_bytes_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientRequestBytesMeasureName)
          .set_name("client_request_bytes")
          .set_aggregation(stats::Aggregation::Distribution(
              stats::BucketBoundaries::Explicit({})))
          .add_column(kMethodTagKey);
  stats::View client_request_bytes_view(client_request_bytes_descriptor);
  const auto client_response_bytes_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientResponseBytesMeasureName)
          .set_name("client_response_bytes")
          .set_aggregation(stats::Aggregation::Distribution(
              stats::BucketBoundaries::Explicit({})))
          .add_column(kMethodTagKey);
  stats::View client_response_bytes_view(client_response_bytes_descriptor);
  const auto server_request_bytes_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerRequestBytesMeasureName)
          .set_name("server_request_bytes")
          .set_aggregation(stats::Aggregation::Distribution(
              stats::BucketBoundaries::Explicit({})))
          .add_column(kMethodTagKey);
  stats::View server_request_bytes_view(server_request_bytes_descriptor);
  const auto server_response_bytes_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerResponseBytesMeasureName)
          .set_name("server_response_bytes")
          .set_aggregation(stats::Aggregation::Distribution(
              stats::BucketBoundaries::Explicit({})))
          .add_column(kMethodTagKey);
  stats::View server_response_bytes_view(server_response_bytes_descriptor);

  {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    ::grpc::ClientContext context;
    ::grpc::Status status = stub_->Echo(&context, request, &response);
    ASSERT_TRUE(status.ok());
    EXPECT_EQ("foo", response.message());
  }

  const auto client_request_bytes_data =
      client_request_bytes_view.GetData().distribution_data();
  ASSERT_EQ(1, client_request_bytes_data.size());
  const auto client_request_bytes =
      client_request_bytes_data.find({method_name_});
  ASSERT_NE(client_request_bytes, client_request_bytes_data.end());
  EXPECT_EQ(1, client_request_bytes->second.count());
  EXPECT_GT(client_request_bytes->second.mean(), 0);

  const auto client_response_bytes_data =
      client_response_bytes_view.GetData().distribution_data();
  ASSERT_EQ(1, client_response_bytes_data.size());
  const auto client_response_bytes =
      client_response_bytes_data.find({method_name_});
  ASSERT_NE(client_response_bytes, client_response_bytes_data.end());
  EXPECT_EQ(1, client_response_bytes->second.count());
  EXPECT_GT(client_response_bytes->second.mean(), 0);

  const auto server_request_bytes_data =
      server_request_bytes_view.GetData().distribution_data();
  ASSERT_EQ(1, server_request_bytes_data.size());
  const auto server_request_bytes =
      server_request_bytes_data.find({method_name_});
  ASSERT_NE(server_request_bytes, server_request_bytes_data.end());
  EXPECT_EQ(1, server_request_bytes->second.count());
  EXPECT_DOUBLE_EQ(server_request_bytes->second.mean(),
                   client_request_bytes->second.mean());

  const auto server_response_bytes_data =
      server_response_bytes_view.GetData().distribution_data();
  ASSERT_EQ(1, server_response_bytes_data.size());
  const auto server_response_bytes =
      server_response_bytes_data.find({method_name_});
  ASSERT_NE(server_response_bytes, server_response_bytes_data.end());
  EXPECT_EQ(1, server_response_bytes->second.count());
  EXPECT_DOUBLE_EQ(server_response_bytes->second.mean(),
                   client_response_bytes->second.mean());
}

TEST_F(StatsPluginEnd2EndTest, Latency) {
  const auto client_latency_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientRoundtripLatencyMeasureName)
          .set_name("client_latency")
          .set_aggregation(stats::Aggregation::Distribution(
              stats::BucketBoundaries::Explicit({})))
          .add_column(kMethodTagKey);
  stats::View client_latency_view(client_latency_descriptor);
  const auto client_server_elapsed_time_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientServerElapsedTimeMeasureName)
          .set_name("client_server_elapsed_time")
          .set_aggregation(stats::Aggregation::Distribution(
              stats::BucketBoundaries::Explicit({})))
          .add_column(kMethodTagKey);
  stats::View client_server_elapsed_time_view(
      client_server_elapsed_time_descriptor);
  const auto server_server_elapsed_time_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerServerElapsedTimeMeasureName)
          .set_name("server_server_elapsed_time")
          .set_aggregation(stats::Aggregation::Distribution(
              stats::BucketBoundaries::Explicit({})))
          .add_column(kMethodTagKey);
  stats::View server_server_elapsed_time_view(
      server_server_elapsed_time_descriptor);

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

  const auto client_latency_data =
      client_latency_view.GetData().distribution_data();
  ASSERT_EQ(1, client_latency_data.size());
  const auto client_latency = client_latency_data.find({method_name_});
  ASSERT_NE(client_latency, client_latency_data.end());
  EXPECT_EQ(1, client_latency->second.count());
  EXPECT_GT(client_latency->second.mean(), 0);
  EXPECT_LT(client_latency->second.mean(), max_time);

  const auto client_server_elapsed_time_data =
      client_server_elapsed_time_view.GetData().distribution_data();
  ASSERT_EQ(1, client_server_elapsed_time_data.size());
  const auto client_server_elapsed_time =
      client_server_elapsed_time_data.find({method_name_});
  ASSERT_NE(client_server_elapsed_time, client_server_elapsed_time_data.end());
  EXPECT_EQ(1, client_server_elapsed_time->second.count());
  EXPECT_GT(client_server_elapsed_time->second.mean(), 0);
  // Elapsed time is a subinterval of total latency.
  EXPECT_LT(client_server_elapsed_time->second.mean(),
            client_latency->second.mean());

  const auto server_server_elapsed_time_data =
      server_server_elapsed_time_view.GetData().distribution_data();
  ASSERT_EQ(1, server_server_elapsed_time_data.size());
  const auto server_server_elapsed_time =
      server_server_elapsed_time_data.find({method_name_});
  ASSERT_NE(server_server_elapsed_time, server_server_elapsed_time_data.end());
  EXPECT_EQ(1, server_server_elapsed_time->second.count());
  // client server elapsed time should be the same value propagated to the
  // client.
  EXPECT_DOUBLE_EQ(server_server_elapsed_time->second.mean(),
                   client_server_elapsed_time->second.mean());
}

TEST_F(StatsPluginEnd2EndTest, StartFinishCount) {
  const auto client_started_count_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientStartedCountMeasureName)
          .set_name("client_started_count")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View client_started_count_view(client_started_count_descriptor);
  const auto client_finished_count_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientFinishedCountMeasureName)
          .set_name("client_finished_count")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View client_finished_count_view(client_finished_count_descriptor);
  const auto server_started_count_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerStartedCountMeasureName)
          .set_name("server_started_count")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View server_started_count_view(server_started_count_descriptor);
  const auto server_finished_count_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerFinishedCountMeasureName)
          .set_name("server_finished_count")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View server_finished_count_view(server_finished_count_descriptor);

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

    EXPECT_THAT(client_started_count_view.GetData().double_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(method_name_), i + 1)));
    EXPECT_THAT(client_finished_count_view.GetData().double_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(method_name_), i + 1)));
    EXPECT_THAT(server_started_count_view.GetData().double_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(method_name_), i + 1)));
    EXPECT_THAT(server_finished_count_view.GetData().double_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(method_name_), i + 1)));
  }
}

TEST_F(StatsPluginEnd2EndTest, RequestResponseCount) {
  // TODO: Use streaming RPCs.
  const auto client_request_count_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientRequestCountMeasureName)
          .set_name("client_request_count")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View client_request_count_view(client_request_count_descriptor);
  const auto client_response_count_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcClientResponseCountMeasureName)
          .set_name("client_response_count")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View client_response_count_view(client_response_count_descriptor);
  const auto server_request_count_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerRequestCountMeasureName)
          .set_name("server_request_count")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View server_request_count_view(server_request_count_descriptor);
  const auto server_response_count_descriptor =
      stats::ViewDescriptor()
          .set_measure(kRpcServerResponseCountMeasureName)
          .set_name("server_response_count")
          .set_aggregation(stats::Aggregation::Sum())
          .add_column(kMethodTagKey);
  stats::View server_response_count_view(server_response_count_descriptor);

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

    EXPECT_THAT(client_request_count_view.GetData().double_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(method_name_), i + 1)));
    EXPECT_THAT(client_response_count_view.GetData().double_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(method_name_), i + 1)));
    EXPECT_THAT(server_request_count_view.GetData().double_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(method_name_), i + 1)));
    EXPECT_THAT(server_response_count_view.GetData().double_data(),
                ::testing::UnorderedElementsAre(::testing::Pair(
                    ::testing::ElementsAre(method_name_), i + 1)));
  }
}

}  // namespace
}  // namespace testing
}  // namespace opencensus

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
