// Copyright 2024 gRPC authors.
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

#include "src/cpp/ext/chaotic_good.h"

#include <grpcpp/create_channel.h>

#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace {

TEST(ChaoticGoodTest, CreateChannel) {
  auto creds = ChaoticGoodInsecureChannelCredentials();
  auto channel = CreateChannel("localhost:50051", creds);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
