// Copyright 2022 The gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"

#include <memory>

#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.h"

namespace grpc_event_engine {
namespace experimental {

TEST(WakeupFdPosixTest, PipeWakeupFdTest) {
  if (!PipeWakeupFd::IsSupported()) {
    return;
  }
  EventEnginePosixInterface posix_interface;
  auto pipe_wakeup_fd = PipeWakeupFd::CreatePipeWakeupFd(&posix_interface);
  EXPECT_TRUE(pipe_wakeup_fd.ok());
  EXPECT_TRUE((*pipe_wakeup_fd)->ReadFd().ready());
  EXPECT_TRUE((*pipe_wakeup_fd)->WriteFd().ready());
  EXPECT_TRUE((*pipe_wakeup_fd)->Wakeup().ok());
  EXPECT_TRUE((*pipe_wakeup_fd)->ConsumeWakeup().ok());
}

TEST(WakeupFdPosixTest, EventFdWakeupFdTest) {
  if (!EventFdWakeupFd::IsSupported()) {
    return;
  }
  EventEnginePosixInterface posix_interface;
  auto eventfd_wakeup_fd =
      EventFdWakeupFd::CreateEventFdWakeupFd(&posix_interface);
  EXPECT_TRUE(eventfd_wakeup_fd.ok());
  EXPECT_TRUE((*eventfd_wakeup_fd)->ReadFd().ready());
  EXPECT_FALSE((*eventfd_wakeup_fd)->WriteFd().ready());
  EXPECT_TRUE((*eventfd_wakeup_fd)->Wakeup().ok());
  EXPECT_TRUE((*eventfd_wakeup_fd)->ConsumeWakeup().ok());
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
