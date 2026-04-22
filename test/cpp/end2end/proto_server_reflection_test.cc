//
//
// Copyright 2016 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/channel_arguments.h>

#include <memory>
#include <mutex>
#include <vector>

#include "src/proto/grpc/reflection/v1/reflection.grpc.pb.h"
#include "src/proto/grpc/reflection/v1/reflection.pb.h"
#include "src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h"
#include "src/proto/grpc/reflection/v1alpha/reflection.pb.h"
#include "src/proto/grpc/health/v1/health.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/end2end_test_utils.h"
#include "test/cpp/util/proto_reflection_descriptor_database.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc {
namespace testing {
namespace {

class ScopedDefaultHealthCheckServiceEnable final {
 public:
  explicit ScopedDefaultHealthCheckServiceEnable(bool enable)
      : lock_(GetMutex()),
        previous_value_(grpc::DefaultHealthCheckServiceEnabled()) {
    grpc::EnableDefaultHealthCheckService(enable);
  }

  ~ScopedDefaultHealthCheckServiceEnable() {
    grpc::EnableDefaultHealthCheckService(previous_value_);
  }

 private:
  static std::mutex& GetMutex() {
    static auto* mu = new std::mutex();
    return *mu;
  }

  std::lock_guard<std::mutex> lock_;
  bool previous_value_;
};

}  // namespace

class ProtoServerReflectionTest : public ::testing::Test {
 public:
  ProtoServerReflectionTest() {}

  void SetUp() override {
    port_ = grpc_pick_unused_port_or_die();
    ref_desc_pool_ = protobuf::DescriptorPool::generated_pool();

    StartServer();
  }

  void StartServer(bool enable_default_health_service = false) {
    std::unique_ptr<ScopedDefaultHealthCheckServiceEnable> health_check_guard;
    if (enable_default_health_service) {
      health_check_guard =
          std::make_unique<ScopedDefaultHealthCheckServiceEnable>(true);
    }
    ServerBuilder builder;
    std::string server_address = "localhost:" + to_string(port_);
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    server_ = builder.BuildAndStart();
  }

  void ResetStub() {
    string target = "dns:localhost:" + to_string(port_);
    ChannelArguments args;
    ApplyCommonChannelArguments(args);
    channel_ =
        grpc::CreateCustomChannel(target, InsecureChannelCredentials(), args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
    desc_db_ = std::make_unique<ProtoReflectionDescriptorDatabase>(channel_);
    desc_pool_ = std::make_unique<protobuf::DescriptorPool>(desc_db_.get());
  }

  string to_string(const int number) {
    std::stringstream strs;
    strs << number;
    return strs.str();
  }

  void CompareService(const std::string& service) {
    const protobuf::ServiceDescriptor* service_desc =
        desc_pool_->FindServiceByName(service);
    const protobuf::ServiceDescriptor* ref_service_desc =
        ref_desc_pool_->FindServiceByName(service);
    EXPECT_TRUE(service_desc != nullptr);
    EXPECT_TRUE(ref_service_desc != nullptr);
    EXPECT_EQ(service_desc->DebugString(), ref_service_desc->DebugString());

    const protobuf::FileDescriptor* file_desc = service_desc->file();
    if (known_files_.find(std::string(file_desc->package()) + "/" +
                          std::string(file_desc->name())) !=
        known_files_.end()) {
      EXPECT_EQ(file_desc->DebugString(),
                ref_service_desc->file()->DebugString());
      known_files_.insert(std::string(file_desc->package()) + "/" +
                          std::string(file_desc->name()));
    }

    for (int i = 0; i < service_desc->method_count(); ++i) {
      CompareMethod(std::string(service_desc->method(i)->full_name()));
    }
  }

  void CompareMethod(const std::string& method) {
    const protobuf::MethodDescriptor* method_desc =
        desc_pool_->FindMethodByName(method);
    const protobuf::MethodDescriptor* ref_method_desc =
        ref_desc_pool_->FindMethodByName(method);
    EXPECT_TRUE(method_desc != nullptr);
    EXPECT_TRUE(ref_method_desc != nullptr);
    EXPECT_EQ(method_desc->DebugString(), ref_method_desc->DebugString());

    CompareType(std::string(method_desc->input_type()->full_name()));
    CompareType(std::string(method_desc->output_type()->full_name()));
  }

  void CompareType(const std::string& type) {
    if (known_types_.find(type) != known_types_.end()) {
      return;
    }

    const protobuf::Descriptor* desc = desc_pool_->FindMessageTypeByName(type);
    const protobuf::Descriptor* ref_desc =
        ref_desc_pool_->FindMessageTypeByName(type);
    EXPECT_TRUE(desc != nullptr);
    EXPECT_TRUE(ref_desc != nullptr);
    EXPECT_EQ(desc->DebugString(), ref_desc->DebugString());
  }

  template <typename Response>
  std::vector<std::string> ServicesFromResponse(const Response& response) {
    std::vector<std::string> services;
    for (const auto& service : response.list_services_response().service()) {
      services.emplace_back(service.name());
    }
    return services;
  }

 protected:
  std::unique_ptr<Server> server_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<ProtoReflectionDescriptorDatabase> desc_db_;
  std::unique_ptr<protobuf::DescriptorPool> desc_pool_;
  std::unordered_set<string> known_files_;
  std::unordered_set<string> known_types_;
  const protobuf::DescriptorPool* ref_desc_pool_;
  int port_;
  reflection::ProtoServerReflectionPlugin plugin_;
};

TEST_F(ProtoServerReflectionTest, CheckResponseWithLocalDescriptorPool) {
  ResetStub();

  std::vector<std::string> services;
  desc_db_->GetServices(&services);
  // The service list has at least one service (reflection service).
  EXPECT_TRUE(!services.empty());

  for (auto it = services.begin(); it != services.end(); ++it) {
    CompareService(*it);
  }
}

TEST_F(ProtoServerReflectionTest, V1AlphaApiInstalled) {
  SKIP_TEST_FOR_PH2_CLIENT(
      "TODO(tjagtap) [PH2][P3][Client] Fix memory leak. The leak is flaky "
      "(4/10 times)");
  ResetStub();
  using Service = reflection::v1alpha::ServerReflection;
  using Request = reflection::v1alpha::ServerReflectionRequest;
  using Response = reflection::v1alpha::ServerReflectionResponse;
  Service::Stub stub(channel_);
  ClientContext context;
  auto reader_writer = stub.ServerReflectionInfo(&context);
  Request request;
  request.set_list_services("*");
  reader_writer->Write(request);
  Response response;
  ASSERT_EQ(reader_writer->Read(&response), true);
  EXPECT_THAT(ServicesFromResponse(response),
              ::testing::UnorderedElementsAre(
                  reflection::v1alpha::ServerReflection::service_full_name(),
                  reflection::v1::ServerReflection::service_full_name()));
  request = Request::default_instance();
  request.set_file_containing_symbol(Service::service_full_name());
  reader_writer->WriteLast(request, WriteOptions());
  response = Response::default_instance();
  ASSERT_EQ(reader_writer->Read(&response), true);
  EXPECT_EQ(response.file_descriptor_response().file_descriptor_proto_size(), 1)
      << response.DebugString();
}

TEST_F(ProtoServerReflectionTest, V1ApiInstalled) {
  SKIP_TEST_FOR_PH2_CLIENT(
      "TODO(tjagtap) [PH2][P3][Client] Fix memory leak. The leak is flaky "
      "(3/10 times)");
  ResetStub();
  using Service = reflection::v1::ServerReflection;
  using Request = reflection::v1::ServerReflectionRequest;
  using Response = reflection::v1::ServerReflectionResponse;
  Service::Stub stub(channel_);
  ClientContext context;
  auto reader_writer = stub.ServerReflectionInfo(&context);
  Request request;
  request.set_list_services("*");
  reader_writer->Write(request);
  Response response;
  ASSERT_TRUE(reader_writer->Read(&response));
  EXPECT_THAT(ServicesFromResponse(response),
              ::testing::UnorderedElementsAre(
                  reflection::v1alpha::ServerReflection::service_full_name(),
                  reflection::v1::ServerReflection::service_full_name()));
  request = Request::default_instance();
  request.set_file_containing_symbol(Service::service_full_name());
  reader_writer->WriteLast(request, WriteOptions());
  response = Response::default_instance();
  ASSERT_TRUE(reader_writer->Read(&response));
  EXPECT_EQ(response.file_descriptor_response().file_descriptor_proto_size(), 1)
      << response.DebugString();
}

TEST_F(ProtoServerReflectionTest, DefaultHealthServiceDescriptorAvailable) {
  server_->Shutdown();
  server_.reset();
  port_ = grpc_pick_unused_port_or_die();
  StartServer(true);
  ResetStub();

  using Service = reflection::v1alpha::ServerReflection;
  using Request = reflection::v1alpha::ServerReflectionRequest;
  using Response = reflection::v1alpha::ServerReflectionResponse;
  Service::Stub stub(channel_);
  ClientContext context;
  auto reader_writer = stub.ServerReflectionInfo(&context);
  Request request;
  request.set_list_services("*");
  ASSERT_TRUE(reader_writer->Write(request));
  Response response;
  ASSERT_TRUE(reader_writer->Read(&response));
  EXPECT_THAT(ServicesFromResponse(response),
              ::testing::Contains("grpc.health.v1.Health"));

  request = Request::default_instance();
  request.set_file_containing_symbol("grpc.health.v1.Health");
  ASSERT_TRUE(reader_writer->WriteLast(request, WriteOptions()));
  response = Response::default_instance();
  ASSERT_TRUE(reader_writer->Read(&response));
  ASSERT_EQ(response.file_descriptor_response().file_descriptor_proto_size(), 1)
      << response.DebugString();
  EXPECT_TRUE(reader_writer->Finish().ok());

  protobuf::FileDescriptorProto file_desc_proto;
  ASSERT_TRUE(file_desc_proto.ParseFromString(
      response.file_descriptor_response().file_descriptor_proto(0)));
  EXPECT_EQ(file_desc_proto.name(),
            grpc::health::v1::HealthCheckRequest::descriptor()->file()->name());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
