// Copyright 2025 gRPC authors.
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

#include "test/cpp/sleuth/client.h"

#include <grpcpp/ext/channelz_service_plugin.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <memory>
#include <string>
#include <vector>

#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "test/core/test_util/port.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace grpc_sleuth {
namespace {

class ClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc::channelz::experimental::InitChannelzService();
    grpc::ServerBuilder builder;
    int port = grpc_pick_unused_port_or_die();
    server_address_ = absl::StrCat("localhost:", port);
    builder.AddListeningPort(server_address_,
                             grpc::InsecureServerCredentials());
    server_ = builder.BuildAndStart();
    ASSERT_NE(server_, nullptr);
  }

  void TearDown() override { server_->Shutdown(); }

  const std::string& server_address() { return server_address_; }

 private:
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
};

TEST_F(ClientTest, QueryAllEntities) {
  Client client(server_address(),
                Client::Options{grpc::InsecureChannelCredentials(), "h2"});
  auto entities = client.QueryAllChannelzEntities();
  ASSERT_TRUE(entities.ok());
  EXPECT_FALSE(entities->empty());
  bool server_found = false;
  for (const auto& entity : *entities) {
    if (entity.kind() == "server") {
      server_found = true;
      break;
    }
  }
  EXPECT_TRUE(server_found);
}

}  // namespace
}  // namespace grpc_sleuth
