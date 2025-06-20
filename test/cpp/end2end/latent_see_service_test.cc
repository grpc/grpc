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

#include "src/cpp/latent_see/latent_see_service.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <memory>
#include <sstream>

#include "gtest/gtest.h"
#include "src/core/util/json/json_reader.h"
#include "src/cpp/latent_see/latent_see_client.h"
#include "src/proto/grpc/channelz/v2/latent_see.grpc.pb.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {
namespace {

TEST(LatentSeeServiceTest, Works) {
  auto service =
      std::make_unique<LatentSeeService>(LatentSeeService::Options());
  ServerBuilder builder;
  builder.RegisterService(service.get());
  auto server = builder.BuildAndStart();
  auto channel = server->InProcessChannel(ChannelArguments());
  auto stub = std::make_unique<channelz::v2::LatentSee::Stub>(channel);
  std::ostringstream out;
  auto output = std::make_unique<grpc_core::latent_see::JsonOutput>(out);
  FetchLatentSee(stub.get(), 1.0, output.get());
  output.reset();
  // just verify the JSON is parsable - we check specifics elsewhere
  auto obj = grpc_core::JsonParse(out.str());
  CHECK_OK(obj);
  server->Shutdown();
  server.reset();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
