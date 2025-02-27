//
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
//

#include "src/core/credentials/transport/insecure/insecure_credentials.h"

#include <grpc/grpc_security.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(InsecureCredentialsTest, CompareSuccess) {
  auto insecure_creds_1 = grpc_insecure_credentials_create();
  auto insecure_creds_2 = grpc_insecure_credentials_create();
  ASSERT_EQ(insecure_creds_1->cmp(insecure_creds_2), 0);
  grpc_arg arg_1 = grpc_channel_credentials_to_arg(insecure_creds_1);
  grpc_channel_args args_1 = {1, &arg_1};
  grpc_arg arg_2 = grpc_channel_credentials_to_arg(insecure_creds_2);
  grpc_channel_args args_2 = {1, &arg_2};
  EXPECT_EQ(grpc_channel_args_compare(&args_1, &args_2), 0);
  grpc_channel_credentials_release(insecure_creds_1);
  grpc_channel_credentials_release(insecure_creds_2);
}

TEST(InsecureCredentialsTest, CompareFailure) {
  auto* insecure_creds = grpc_insecure_credentials_create();
  auto* fake_creds = grpc_fake_transport_security_credentials_create();
  ASSERT_NE(insecure_creds->cmp(fake_creds), 0);
  ASSERT_NE(fake_creds->cmp(insecure_creds), 0);
  grpc_arg arg_1 = grpc_channel_credentials_to_arg(insecure_creds);
  grpc_channel_args args_1 = {1, &arg_1};
  grpc_arg arg_2 = grpc_channel_credentials_to_arg(fake_creds);
  grpc_channel_args args_2 = {1, &arg_2};
  EXPECT_NE(grpc_channel_args_compare(&args_1, &args_2), 0);
  grpc_channel_credentials_release(fake_creds);
  grpc_channel_credentials_release(insecure_creds);
}

TEST(InsecureCredentialsTest, SingletonCreate) {
  auto* insecure_creds_1 = grpc_insecure_credentials_create();
  auto* insecure_creds_2 = grpc_insecure_credentials_create();
  EXPECT_EQ(insecure_creds_1, insecure_creds_2);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
