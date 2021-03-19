//
//
// Copyright 2021 gRPC authors.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpcpp/ext/admin_services.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include "src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {

namespace {

std::string GetAddress() {
  std::ostringstream s;
  int p = grpc_pick_unused_port_or_die();
  s << "localhost:" << p;
  return s.str();
}

}  // namespace

class AdminServicesTest : public ::testing::Test {
 public:
  void SetUp() {
    std::string address = GetAddress();
    // Create admin server
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    builder.AddListeningPort(address, InsecureServerCredentials());
    ::grpc::experimental::AddAdminServices(&builder);
    server_ = builder.BuildAndStart();
    // Create channel
    channel_ = CreateChannel(address, InsecureChannelCredentials());
    auto reflection_stub =
        reflection::v1alpha::ServerReflection::NewStub(channel_);
    stream_ = reflection_stub->ServerReflectionInfo(&reflection_ctx_);
  }

  std::vector<std::string> GetServiceList() {
    std::vector<std::string> services;
    reflection::v1alpha::ServerReflectionRequest request;
    reflection::v1alpha::ServerReflectionResponse response;
    request.set_list_services("");
    stream_->Write(request);
    stream_->Read(&response);
    for (auto& service : response.list_services_response().service()) {
      services.push_back(service.name());
    }
    return services;
  }

  std::shared_ptr<Channel> GetChannel() { return channel_; }

 private:
  std::unique_ptr<Server> server_;
  std::shared_ptr<Channel> channel_;
  ClientContext reflection_ctx_;
  std::shared_ptr<
      ClientReaderWriter<reflection::v1alpha::ServerReflectionRequest,
                         reflection::v1alpha::ServerReflectionResponse>>
      stream_;
};

#ifndef GRPC_NO_XDS
TEST_F(AdminServicesTest, XdsEnabled) {
  EXPECT_THAT(GetServiceList(),
              ::testing::UnorderedElementsAre(
                  "envoy.service.status.v3.ClientStatusDiscoveryService",
                  "grpc.channelz.v1.Channelz",
                  "grpc.reflection.v1alpha.ServerReflection"));
}
#endif  // GRPC_NO_XDS

#ifdef GRPC_NO_XDS
TEST_F(AdminServicesTest, XdsDisabled) {
  EXPECT_THAT(GetServiceList(),
              ::testing::UnorderedElementsAre(
                  "grpc.channelz.v1.Channelz",
                  "grpc.reflection.v1alpha.ServerReflection"));
}
#endif  // GRPC_NO_XDS

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
