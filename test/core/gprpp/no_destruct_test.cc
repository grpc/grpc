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

#include "src/core/lib/gprpp/no_destruct.h"

#include <stdlib.h>

#include <memory>

#include <gtest/gtest.h>

namespace grpc_core {
namespace testing {
namespace {

class CrashOnDestruction {
 public:
  void Exists() {}

 private:
  ~CrashOnDestruction() { abort(); }
};

NoDestruct<std::unique_ptr<int>> g_test_int(new int(42));
NoDestruct<CrashOnDestruction> g_test_crash_on_destruction;

TEST(NoDestruct, Works) { EXPECT_EQ(42, **g_test_int); }

TEST(NoDestruct, CrashOnDestructionIsAccessible) {
  g_test_crash_on_destruction->Exists();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
