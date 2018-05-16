/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/channel/status_util.h"

#include <gtest/gtest.h>

namespace grpc_core {
namespace internal {
namespace {

TEST(StatusCodeSet, Basic) {
  StatusCodeSet set;
  EXPECT_TRUE(set.Empty());
  EXPECT_FALSE(set.Contains(GRPC_STATUS_OK));
  EXPECT_FALSE(set.Contains(GRPC_STATUS_UNAVAILABLE));
  set.Add(GRPC_STATUS_OK);
  EXPECT_FALSE(set.Empty());
  EXPECT_TRUE(set.Contains(GRPC_STATUS_OK));
  EXPECT_FALSE(set.Contains(GRPC_STATUS_UNAVAILABLE));
  set.Add(GRPC_STATUS_UNAVAILABLE);
  EXPECT_FALSE(set.Empty());
  EXPECT_TRUE(set.Contains(GRPC_STATUS_OK));
  EXPECT_TRUE(set.Contains(GRPC_STATUS_UNAVAILABLE));
}

}  // namespace
}  // namespace internal
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
