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

#include "src/core/lib/promise/all_ok.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/utility/utility.h"
#include "gtest/gtest.h"

namespace grpc_core {

using P = std::function<Poll<StatusFlag>()>;

P instant_success() {
  return [] { return Success{}; };
}

P instant_fail() {
  return [] { return Failure{}; };
}

Poll<StatusFlag> succeeded() { return Poll<StatusFlag>(Success{}); }

Poll<StatusFlag> failed() { return Poll<StatusFlag>(Failure{}); }

TEST(AllOkTest, Join2) {
  EXPECT_EQ(AllOk<StatusFlag>(instant_fail(), instant_fail())(), failed());
  EXPECT_EQ(AllOk<StatusFlag>(instant_fail(), instant_success())(), failed());
  EXPECT_EQ(AllOk<StatusFlag>(instant_success(), instant_fail())(), failed());
  EXPECT_EQ(AllOk<StatusFlag>(instant_success(), instant_success())(),
            succeeded());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
