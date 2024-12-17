// Copyright 2024 gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/file_descriptors.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"

namespace grpc_event_engine {
namespace experimental {

TEST(FileDescriptorsTest, WaitsForLocksToDrop) {
  FileDescriptors fds;
  std::vector<ReentrantLock> locks;
  locks.reserve(5);
  while (locks.size() < 5) {
    auto lock = fds.PosixLock();
    ASSERT_TRUE(lock.ok()) << lock.status();
    locks.emplace_back(std::move(lock).value());
  }
  grpc_core::Mutex mu;
  grpc_core::CondVar cond;
  absl::optional<absl::Status> stop_status;
  std::thread fds_stop_thread([&]() {
    auto status = fds.Stop();
    grpc_core::MutexLock lock(&mu);
    stop_status = status;
    cond.SignalAll();
  });
  auto thread_cleanup = absl::MakeCleanup([&]() { fds_stop_thread.join(); });
  fds.ExpectStatusForTest(locks.size(), FileDescriptors::State::kStopping);
  while (locks.size() > 1) {
    locks.pop_back();
  }
  fds.ExpectStatusForTest(locks.size(), FileDescriptors::State::kStopping);
  auto failed_lock = fds.PosixLock();
  EXPECT_EQ(failed_lock.status().code(), absl::StatusCode::kAborted);
  locks.clear();
  fds.ExpectStatusForTest(0, FileDescriptors::State::kStopped);
  grpc_core::MutexLock lock(&mu);
  while (!stop_status.has_value()) {
    cond.Wait(&mu);
  }
  EXPECT_TRUE(stop_status->ok()) << *stop_status;
  fds.Restart();
  fds.ExpectStatusForTest(locks.size(), FileDescriptors::State::kReady);
  auto l = fds.PosixLock();
  EXPECT_TRUE(l.ok()) << l.status();
}

// This diagnostic was problematic from implementation pov
TEST(FileDescriptorsTest, DISABLED_DetectsIfThreadHasIOLock) {
  FileDescriptors fds;
  FileDescriptor fd = fds.Add(1);
  auto locked = fds.Lock(fd);
  LOG(INFO) << "before";
  absl::Status status = fds.Stop();
  LOG(INFO) << 5;
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition) << status;
  LOG(INFO) << 6;
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
