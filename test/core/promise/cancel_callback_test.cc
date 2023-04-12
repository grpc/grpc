// Copyright 2022 gRPC authors.
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

#include "src/core/lib/promise/cancel_callback.h"

#include "gtest/gtest.h"

#include "src/core/lib/promise/poll.h"

namespace grpc_core {

TEST(CancelCallback, DoesntCallCancelIfCompleted) {
  auto x = OnCancel([]() { return 42; },
                    []() { FAIL() << "Should never reach here"; });
  EXPECT_EQ(x(), Poll<int>(42));
}

TEST(CancelCallback, CallsCancelIfNotCompleted) {
  bool called = false;
  {
    auto x = OnCancel([]() { return 42; }, [&called]() { called = true; });
    EXPECT_EQ(called, false);
  }
  EXPECT_EQ(called, true);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
