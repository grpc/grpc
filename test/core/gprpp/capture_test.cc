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

#include "src/core/lib/gprpp/capture.h"

#include <gtest/gtest.h>

namespace grpc_core {

TEST(CaptureTest, Capture) {
  auto f = Capture([](int* p) { EXPECT_EQ(*p, 42); }, 42);
  f();
}

TEST(CaptureTest, WithArgsAndReturn) {
  int captured = 1;
  auto f =
      Capture([captured](int* p, int arg) { return (captured + *p) * arg; }, 2);
  EXPECT_EQ(f(2), 6);
  EXPECT_EQ(f(3), 9);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
