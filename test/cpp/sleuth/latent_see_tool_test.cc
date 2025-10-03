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

#include <grpcpp/security/credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <memory>
#include <optional>
#include <string>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/cpp/latent_see/latent_see_service.h"
#include "test/core/test_util/port.h"
#include "test/cpp/sleuth/tool_test.h"

ABSL_DECLARE_FLAG(std::optional<std::string>, latent_see_target);
ABSL_DECLARE_FLAG(std::string, channel_creds_type);

namespace grpc_sleuth {
namespace {

class LatentSeeToolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc::ServerBuilder builder;
    int port = grpc_pick_unused_port_or_die();
    server_address_ = absl::StrCat("localhost:", port);
    builder.AddListeningPort(server_address_,
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&latent_see_service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(server_, nullptr);
  }

  void TearDown() override { server_->Shutdown(); }

  const std::string& server_address() const { return server_address_; }

 private:
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  grpc::LatentSeeService latent_see_service_{grpc::LatentSeeService::Options()};
};

TEST_F(LatentSeeToolTest, FetchLatentSeeJson) {
  absl::SetFlag(&FLAGS_latent_see_target, server_address());
  absl::SetFlag(&FLAGS_channel_creds_type, "insecure");
  auto result = TestTool("fetch_latent_see_json", {"-"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_FALSE(result->empty());
}

}  // namespace
}  // namespace grpc_sleuth
