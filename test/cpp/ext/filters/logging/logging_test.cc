//
//
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
//
//

#include <chrono>
#include <thread>  // NOLINT

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

#include <grpc++/grpc++.h>
#include <grpcpp/support/status.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/cpp/ext/filters/logging/logging_filter.h"
#include "src/cpp/ext/gcp/observability_logging_sink.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {

namespace {

using grpc::internal::LoggingSink;

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

class MyTestServiceImpl : public TestServiceImpl {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    context->AddInitialMetadata("server-header-key", "server-header-value");
    context->AddTrailingMetadata("server-trailer-key", "server-trailer-value");
    return TestServiceImpl::Echo(context, request, response);
  }
};

class TestLoggingSink : public grpc::internal::LoggingSink {
 public:
  Config FindMatch(bool /* is_client */, absl::string_view /* service */,
                   absl::string_view /* method */) override {
    grpc_core::MutexLock lock(&mu_);
    return config_;
  }

  void LogEntry(Entry entry) override {
    ::google::protobuf::Struct json;
    grpc::internal::EntryToJsonStructProto(entry, &json);
    std::string output;
    ::google::protobuf::TextFormat::PrintToString(json, &output);
    gpr_log(GPR_ERROR, "%s", output.c_str());
    grpc_core::MutexLock lock(&mu_);
    entries_.push_back(std::move(entry));
  }

  void SetConfig(Config config) {
    grpc_core::MutexLock lock(&mu_);
    config_ = config;
  }

  std::vector<LoggingSink::Entry> entries() {
    grpc_core::MutexLock lock(&mu_);
    return entries_;
  }

  void Clear() {
    grpc_core::MutexLock lock(&mu_);
    entries_.clear();
  }

 private:
  grpc_core::Mutex mu_;
  std::vector<LoggingSink::Entry> entries_ ABSL_GUARDED_BY(mu_);
  Config config_ ABSL_GUARDED_BY(mu_);
};

TestLoggingSink* g_test_logging_sink = nullptr;

class LoggingTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    g_test_logging_sink = new TestLoggingSink;
    grpc::internal::RegisterLoggingFilter(g_test_logging_sink);
  }

  void SetUp() override {
    // Clean up previous entries
    g_test_logging_sink->Clear();
    // Set up a synchronous server on a different thread to avoid the asynch
    // interface.
    grpc::ServerBuilder builder;
    int port = grpc_pick_unused_port_or_die();
    server_address_ = absl::StrCat("localhost:", port);
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);

    server_thread_ = std::thread(&LoggingTest::RunServerLoop, this);

    stub_ = EchoTestService::NewStub(grpc::CreateChannel(
        server_address_, grpc::InsecureChannelCredentials()));
  }

  void ResetStub(std::shared_ptr<Channel> channel) {
    stub_ = EchoTestService::NewStub(std::move(channel));
  }

  void TearDown() override {
    server_->Shutdown();
    server_thread_.join();
  }

  void RunServerLoop() { server_->Wait(); }

  std::string server_address_;
  MyTestServiceImpl service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;

  std::unique_ptr<EchoTestService::Stub> stub_;
};

TEST_F(LoggingTest, DISABLED_SimpleRpc) {
  g_test_logging_sink->SetConfig(
      grpc::internal::LoggingSink::Config(4096, 4096));
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("client-key", "client-value");
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_THAT(
      g_test_logging_sink->entries(),
      ::testing::UnorderedElementsAre(
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("client-key", "client-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair(
                                "server-header-key", "server-header-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(
              Field(&LoggingSink::Entry::type,
                    Eq(LoggingSink::Entry::EventType::kServerTrailer)),
              Field(&LoggingSink::Entry::logger,
                    Eq(LoggingSink::Entry::Logger::kClient)),
              Field(&LoggingSink::Entry::authority, Eq(server_address_)),
              Field(&LoggingSink::Entry::service_name,
                    Eq("grpc.testing.EchoTestService")),
              Field(&LoggingSink::Entry::method_name, Eq("Echo")),
              Field(&LoggingSink::Entry::payload,
                    Field(&LoggingSink::Entry::Payload::metadata,
                          UnorderedElementsAre(Pair("server-trailer-key",
                                                    "server-trailer-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("client-key", "client-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair(
                                "server-header-key", "server-header-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("server-trailer-key",
                                     "server-trailer-value")))))));
}

TEST_F(LoggingTest, DISABLED_LoggingDisabled) {
  g_test_logging_sink->SetConfig(grpc::internal::LoggingSink::Config());
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("client-key", "client-value");
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(g_test_logging_sink->entries().empty());
}

TEST_F(LoggingTest, DISABLED_MetadataTruncated) {
  g_test_logging_sink->SetConfig(grpc::internal::LoggingSink::Config(
      40 /* expect truncated metadata*/, 4096));
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("client-key", "client-value");
  context.AddMetadata("client-key-2", "client-value-2");
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_THAT(
      g_test_logging_sink->entries(),
      ::testing::UnorderedElementsAre(
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("client-key", "client-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair(
                                "server-header-key", "server-header-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(
              Field(&LoggingSink::Entry::type,
                    Eq(LoggingSink::Entry::EventType::kServerTrailer)),
              Field(&LoggingSink::Entry::logger,
                    Eq(LoggingSink::Entry::Logger::kClient)),
              Field(&LoggingSink::Entry::authority, Eq(server_address_)),
              Field(&LoggingSink::Entry::service_name,
                    Eq("grpc.testing.EchoTestService")),
              Field(&LoggingSink::Entry::method_name, Eq("Echo")),
              Field(&LoggingSink::Entry::payload,
                    Field(&LoggingSink::Entry::Payload::metadata,
                          UnorderedElementsAre(Pair("server-trailer-key",
                                                    "server-trailer-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("client-key", "client-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair(
                                "server-header-key", "server-header-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(5)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\003foo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("server-trailer-key",
                                     "server-trailer-value")))))));
}

TEST_F(LoggingTest, DISABLED_PayloadTruncated) {
  g_test_logging_sink->SetConfig(grpc::internal::LoggingSink::Config(4096, 10));
  EchoRequest request;
  // The following message should get truncated
  request.set_message("Hello World");
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("client-key", "client-value");
  grpc::Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_THAT(
      g_test_logging_sink->entries(),
      ::testing::UnorderedElementsAre(
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("client-key", "client-value"))))),
          AllOf(
              Field(&LoggingSink::Entry::type,
                    Eq(LoggingSink::Entry::EventType::kClientMessage)),
              Field(&LoggingSink::Entry::logger,
                    Eq(LoggingSink::Entry::Logger::kClient)),
              Field(&LoggingSink::Entry::authority, Eq(server_address_)),
              Field(&LoggingSink::Entry::service_name,
                    Eq("grpc.testing.EchoTestService")),
              Field(&LoggingSink::Entry::method_name, Eq("Echo")),
              Field(&LoggingSink::Entry::payload,
                    AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                Eq(13)),
                          Field(&LoggingSink::Entry::Payload::message,
                                Eq("\n\013Hello Wo") /* truncated message */))),
              Field(&LoggingSink::Entry::payload_truncated, true)),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair(
                                "server-header-key", "server-header-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kClient)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(13)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\013Hello Wo"))))),
          AllOf(
              Field(&LoggingSink::Entry::type,
                    Eq(LoggingSink::Entry::EventType::kServerTrailer)),
              Field(&LoggingSink::Entry::logger,
                    Eq(LoggingSink::Entry::Logger::kClient)),
              Field(&LoggingSink::Entry::authority, Eq(server_address_)),
              Field(&LoggingSink::Entry::service_name,
                    Eq("grpc.testing.EchoTestService")),
              Field(&LoggingSink::Entry::method_name, Eq("Echo")),
              Field(&LoggingSink::Entry::payload,
                    Field(&LoggingSink::Entry::Payload::metadata,
                          UnorderedElementsAre(Pair("server-trailer-key",
                                                    "server-trailer-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("client-key", "client-value"))))),
          AllOf(
              Field(&LoggingSink::Entry::type,
                    Eq(LoggingSink::Entry::EventType::kClientMessage)),
              Field(&LoggingSink::Entry::logger,
                    Eq(LoggingSink::Entry::Logger::kServer)),
              Field(&LoggingSink::Entry::authority, Eq(server_address_)),
              Field(&LoggingSink::Entry::service_name,
                    Eq("grpc.testing.EchoTestService")),
              Field(&LoggingSink::Entry::method_name, Eq("Echo")),
              Field(&LoggingSink::Entry::payload,
                    AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                Eq(13)),
                          Field(&LoggingSink::Entry::Payload::message,
                                Eq("\n\013Hello Wo") /* truncated message */))),
              Field(&LoggingSink::Entry::payload_truncated, true)),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kClientHalfClose)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo"))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerHeader)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(Pair(
                                "server-header-key", "server-header-value"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerMessage)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      AllOf(Field(&LoggingSink::Entry::Payload::message_length,
                                  Eq(13)),
                            Field(&LoggingSink::Entry::Payload::message,
                                  Eq("\n\013Hello Wo"))))),
          AllOf(Field(&LoggingSink::Entry::type,
                      Eq(LoggingSink::Entry::EventType::kServerTrailer)),
                Field(&LoggingSink::Entry::logger,
                      Eq(LoggingSink::Entry::Logger::kServer)),
                Field(&LoggingSink::Entry::authority, Eq(server_address_)),
                Field(&LoggingSink::Entry::service_name,
                      Eq("grpc.testing.EchoTestService")),
                Field(&LoggingSink::Entry::method_name, Eq("Echo")),
                Field(&LoggingSink::Entry::payload,
                      Field(&LoggingSink::Entry::Payload::metadata,
                            UnorderedElementsAre(
                                Pair("server-trailer-key",
                                     "server-trailer-value")))))));
}

TEST_F(LoggingTest, DISABLED_CancelledRpc) {
  g_test_logging_sink->SetConfig(
      grpc::internal::LoggingSink::Config(4096, 4096));
  EchoRequest request;
  request.set_message("foo");
  const int kCancelDelayUs = 10 * 1000;
  request.mutable_param()->set_client_cancel_after_us(kCancelDelayUs);
  EchoResponse response;
  grpc::ClientContext context;
  context.AddMetadata("client-key", "client-value");
  auto cancel_thread = std::thread(
      [&context, this](int delay) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay));
        while (!service_.signal_client()) {
        }
        context.TryCancel();
      },
      kCancelDelayUs);
  grpc::Status status = stub_->Echo(&context, request, &response);
  cancel_thread.join();
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
  auto initial_time = absl::Now();
  while (true) {
    bool found_cancel_on_client = false;
    bool found_cancel_on_server = false;
    for (const auto& entry : g_test_logging_sink->entries()) {
      if (entry.type == LoggingSink::Entry::EventType::kCancel) {
        if (entry.logger == LoggingSink::Entry::Logger::kClient) {
          found_cancel_on_client = true;
        } else {
          found_cancel_on_server = true;
        }
      }
    }
    if (found_cancel_on_client && found_cancel_on_server) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_LT(absl::Now() - initial_time, absl::Seconds(10));
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
