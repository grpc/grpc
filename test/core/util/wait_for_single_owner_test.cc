// Copyright 2024 gRPC authors.
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

#include "src/core/util/wait_for_single_owner.h"

#include <memory>
#include <thread>

#include "gtest/gtest.h"

TEST(WaitForSingleOwner, Finishes) {
  auto i = std::make_shared<int>(3);
  grpc_core::WaitForSingleOwner(std::move(i));
}

TEST(WaitForSingleOwner, DoesNotFinishWithAHeldInstance) {
  auto i = std::make_shared<int>(3);
  auto timeout = grpc_core::Duration::Seconds(1);
  auto start = grpc_core::Timestamp::Now();
  std::thread t{[i, &timeout]() {
    // Keeps i alive for a short fixed time
    absl::SleepFor(absl::Milliseconds(timeout.millis()));
  }};
  grpc_core::WaitForSingleOwner(std::move(i));
  auto duration = grpc_core::Timestamp::Now() - start;
  ASSERT_GE(duration, timeout);
  t.join();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
