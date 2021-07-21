// Copyright 2021 The gRPC Authors
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

#include "grpc/event_engine/slice.h"

#include <gmock/gmock.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "test/core/util/test_config.h"

using ::grpc_event_engine::experimental::Slice;

TEST(SliceTest, AssignmentConstructorLeavesOriginalSliceUnchanged) {
  char data[6] = "12345";
  Slice s(data, 6);
  Slice s2 = s;
  EXPECT_EQ(s.size(), 6);
  EXPECT_EQ(s2.size(), 6);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
