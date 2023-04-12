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

#include "src/core/lib/gprpp/notification.h"

#include <thread>

#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(Notification, Works) {
  Notification n;
  EXPECT_FALSE(n.HasBeenNotified());
  n.Notify();
  EXPECT_TRUE(n.HasBeenNotified());
  n.WaitForNotification();
  EXPECT_TRUE(n.HasBeenNotified());
}

TEST(Notification, Waits) {
  Notification n;
  auto start = absl::Now();
  std::thread t([&n] {
    absl::SleepFor(absl::Seconds(6));
    n.Notify();
  });
  n.WaitForNotification();
  auto end = absl::Now();
  EXPECT_GE(end - start, absl::Seconds(5));
  t.join();
}

TEST(Notification, WaitsWithTimeout) {
  Notification n;
  auto start = absl::Now();
  std::thread t([&n] {
    absl::SleepFor(absl::Seconds(6));
    n.Notify();
  });
  EXPECT_TRUE(n.WaitForNotificationWithTimeout(absl::Seconds(10)));
  auto end = absl::Now();
  EXPECT_GE(end - start, absl::Seconds(5));
  EXPECT_LE(end - start, absl::Seconds(10));
  t.join();
}

TEST(Notification, WaitWithTimeoutCanFinishEarly) {
  Notification n;
  auto start = absl::Now();
  std::thread t([&n] {
    absl::SleepFor(absl::Seconds(6));
    n.Notify();
  });
  EXPECT_FALSE(n.WaitForNotificationWithTimeout(absl::Seconds(1)));
  auto end = absl::Now();
  EXPECT_GE(end - start, absl::Seconds(1));
  EXPECT_LE(end - start, absl::Seconds(5));
  n.WaitForNotification();
  end = absl::Now();
  EXPECT_GE(end - start, absl::Seconds(5));
  t.join();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
