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

#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"

#include <stdlib.h>

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

TEST(ExecCtxWakeupSchedulerTest, Works) {
  int state = 0;
  bool done = false;
  auto activity = MakeActivity(
      [&state]() mutable -> Poll<absl::Status> {
        ++state;
        switch (state) {
          case 1:
            return Pending();
          case 2:
            return absl::OkStatus();
          default:
            abort();
        }
      },
      ExecCtxWakeupScheduler(),
      [&done](absl::Status status) {
        EXPECT_EQ(status, absl::OkStatus());
        done = true;
      });

  EXPECT_EQ(state, 1);
  EXPECT_FALSE(done);
  {
    ExecCtx exec_ctx;
    EXPECT_FALSE(exec_ctx.HasWork());
    activity->ForceWakeup();
    EXPECT_TRUE(exec_ctx.HasWork());
    EXPECT_EQ(state, 1);
    EXPECT_FALSE(done);
  }
  EXPECT_EQ(state, 2);
  EXPECT_TRUE(done);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
