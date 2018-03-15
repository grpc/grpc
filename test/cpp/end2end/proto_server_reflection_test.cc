/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/proto_reflection_descriptor_database.h"

#include <gtest/gtest.h>

namespace grpc {
namespace testing {

class ProtoServerReflectionTest : public ::testing::Test {
 public:
  ProtoServerReflectionTest() {}

  void SetUp() override {
    port_ = grpc_pick_unused_port_or_die();
    ref_desc_pool_ = protobuf::DescriptorPool::generated_pool();

    ServerBuilder builder;
    grpc::string server_address = "localhost:" + to_string(port_);
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    server_ = builder.BuildAndStart();
  }

  void ResetStub() {
    string target = "dns:localhost:" + to_string(port_);
    std::shared_ptr<Channel> channel =
        CreateChannel(target, InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
    desc_db_.reset(new ProtoReflectionDescriptorDatabase(channel));
    desc_pool_.reset(new protobuf::DescriptorPool(desc_db_.get()));
  }

  string to_string(const int number) {
    std::stringstream strs;
    strs << number;
    return strs.str();
  }

  void CompareService(const grpc::string& service) {
    const protobuf::ServiceDescriptor* service_desc =
        desc_pool_->FindServiceByName(service);
    const protobuf::ServiceDescriptor* ref_service_desc =
        ref_desc_pool_->FindServiceByName(service);
    EXPECT_TRUE(service_desc != nullptr);
    EXPECT_TRUE(ref_service_desc != nullptr);
    EXPECT_EQ(service_desc->DebugString(), ref_service_desc->DebugString());

    const protobuf::FileDescriptor* file_desc = service_desc->file();
    if (known_files_.find(file_desc->package() + "/" + file_desc->name()) !=
        known_files_.end()) {
      EXPECT_EQ(file_desc->DebugString(),
                ref_service_desc->file()->DebugString());
      known_files_.insert(file_desc->package() + "/" + file_desc->name());
    }

    for (int i = 0; i < service_desc->method_count(); ++i) {
      CompareMethod(service_desc->method(i)->full_name());
    }
  }

  void CompareMethod(const grpc::string& method) {
    const protobuf::MethodDescriptor* method_desc =
        desc_pool_->FindMethodByName(method);
    const protobuf::MethodDescriptor* ref_method_desc =
        ref_desc_pool_->FindMethodByName(method);
    EXPECT_TRUE(method_desc != nullptr);
    EXPECT_TRUE(ref_method_desc != nullptr);
    EXPECT_EQ(method_desc->DebugString(), ref_method_desc->DebugString());

    CompareType(method_desc->input_type()->full_name());
    CompareType(method_desc->output_type()->full_name());
  }

  void CompareType(const grpc::string& type) {
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

 protected:
  std::unique_ptr<Server> server_;
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

  std::vector<grpc::string> services;
  desc_db_->GetServices(&services);
  // The service list has at least one service (reflection servcie).
  EXPECT_TRUE(services.size() > 0);

  for (auto it = services.begin(); it != services.end(); ++it) {
    CompareService(*it);
  }
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
