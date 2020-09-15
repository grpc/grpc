// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <gtest/gtest.h>
#include "absl/strings/string_view.h"

#include "src/core/lib/security/authorization/evaluate_args.h"
#include "test/core/util/eval_args_mock_endpoint.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

class EvaluateArgsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    local_address_ = "255.255.255.255";
    peer_address_ = "128.128.128.128";
    local_port_ = 413;
    peer_port_ = 314;
    endpoint_ = CreateEvalArgsMockEndpoint(local_address_.c_str(), local_port_,
                                           peer_address_.c_str(), peer_port_);
    evaluate_args_ =
        absl::make_unique<EvaluateArgs>(nullptr, nullptr, endpoint_);
  }
  void TearDown() override { grpc_endpoint_destroy(endpoint_); }
  grpc_endpoint* endpoint_;
  std::unique_ptr<EvaluateArgs> evaluate_args_;
  std::string local_address_;
  std::string peer_address_;
  int local_port_;
  int peer_port_;
};

TEST_F(EvaluateArgsTest, TestEvaluateArgsLocalAddress) {
  absl::string_view src_address = evaluate_args_->GetLocalAddress();
  EXPECT_EQ(src_address, local_address_)
      << "Error: Failed to extract correct Local address from EvaluateArgs.";
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsLocalPort) {
  int src_port = evaluate_args_->GetLocalPort();
  EXPECT_EQ(src_port, local_port_)
      << "Error: Failed to extract correct Local port from EvaluateArgs.";
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsPeerAddress) {
  absl::string_view dest_address = evaluate_args_->GetPeerAddress();
  EXPECT_EQ(dest_address, peer_address_)
      << "Error: Failed to extract correct Peer address from "
         "EvaluateArgs. ";
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsPeerPort) {
  int dest_port = evaluate_args_->GetPeerPort();
  EXPECT_EQ(dest_port, peer_port_)
      << "Error: Failed to extract correct Peer port from EvaluateArgs.";
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
