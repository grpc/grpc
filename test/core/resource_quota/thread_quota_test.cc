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

#include "src/core/lib/resource_quota/thread_quota.h"

#include <memory>

#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {

TEST(ThreadQuotaTest, Works) {
  auto q = MakeRefCounted<ThreadQuota>();
  EXPECT_TRUE(q->Reserve(128));
  q->SetMax(10);
  EXPECT_FALSE(q->Reserve(128));
  EXPECT_FALSE(q->Reserve(1));
  q->Release(118);
  EXPECT_FALSE(q->Reserve(1));
  q->Release(1);
  EXPECT_TRUE(q->Reserve(1));
  EXPECT_FALSE(q->Reserve(1));
  q->Release(10);
}

}  // namespace testing
}  // namespace grpc_core

// Hook needed to run ExecCtx outside of iomgr.
void grpc_set_default_iomgr_platform() {}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
