/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/ext/proto_server_reflection_plugin.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/proto_reflection_descriptor_database.h"

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
