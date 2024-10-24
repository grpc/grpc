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

#include "src/core/lib/resource_quota/resource_quota.h"

#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {

TEST(ResourceQuotaTest, Works) {
  auto q = MakeRefCounted<ResourceQuota>("foo");
  EXPECT_NE(q->thread_quota(), nullptr);
  EXPECT_NE(q->memory_quota(), nullptr);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment give_me_a_name(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
