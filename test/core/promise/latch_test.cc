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

#include "src/core/lib/promise/latch.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/seq.h"

using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

TEST(LatchTest, Works) {
  Latch<int> latch;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&latch] {
        return Seq(Join(latch.Wait(),
                        [&latch]() {
                          latch.Set(42);
                          return true;
                        }),
                   [](std::tuple<int*, bool> result) {
                     EXPECT_EQ(*std::get<0>(result), 42);
                     return absl::OkStatus();
                   });
      },
      NoCallbackScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
