// Copyright 2023 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/max_concurrent_streams_policy.h"

#include <memory>

#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(MaxConcurrentStreamsPolicyTest, NoOpWorks) {
  Chttp2MaxConcurrentStreamsPolicy policy;
  policy.SetTarget(100);
  EXPECT_EQ(policy.AdvertiseValue(), 100);
}

TEST(MaxConcurrentStreamsPolicyTest, BasicFlow) {
  Chttp2MaxConcurrentStreamsPolicy policy;
  policy.SetTarget(100);
  EXPECT_EQ(policy.AdvertiseValue(), 100);
  policy.AddDemerit();
  EXPECT_EQ(policy.AdvertiseValue(), 99);
  policy.FlushedSettings();
  EXPECT_EQ(policy.AdvertiseValue(), 99);
  policy.AckLastSend();
  EXPECT_EQ(policy.AdvertiseValue(), 100);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
