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

#include "src/core/lib/promise/wait_for_callback.h"

#include "gtest/gtest.h"

#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/promise/map.h"
#include "test/core/promise/test_wakeup_schedulers.h"

namespace grpc_core {

TEST(WaitForCallbackTest, Works) {
  WaitForCallback w4cb;
  auto callback = w4cb.MakeCallback();
  Notification done;
  auto activity = MakeActivity(
      [&w4cb]() {
        return Map(w4cb.MakeWaitPromise(),
                   [](Empty) { return absl::OkStatus(); });
      },
      InlineWakeupScheduler{},
      [&done](const absl::Status& s) {
        EXPECT_TRUE(s.ok());
        done.Notify();
      });
  EXPECT_FALSE(done.HasBeenNotified());
  callback();
  done.WaitForNotification();
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
