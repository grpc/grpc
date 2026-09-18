//
//
// Copyright 2026 gRPC authors.
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
//
//

#include <tuple>
#include <utility>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/promise/test_wakeup_schedulers.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {

TEST(ClientInitialMetadataOutstandingTokenTest, MoveAssignmentCleansUp) {
  if (!IsMetadataOutstandingTokenRefactorEnabled()) {
    GTEST_SKIP()
        << "Experiment client_initial_metadata_outstanding_token_refactor not "
           "enabled";
  }
  auto arena = Arena::Create(/*initial_size=*/1024, SimpleArenaAllocator());

  auto token1 = ClientInitialMetadataOutstandingToken::New(arena.get());
  auto token2 = ClientInitialMetadataOutstandingToken::New(arena.get());

  bool wait1_resolved = false;
  bool wait2_resolved = false;

  auto activity = MakeActivity(
      [&token1, &token2, &wait1_resolved, &wait2_resolved]() {
        return Seq(
            Join(
                // Promise 1: Wait on token1. It should resolve to `false`
                // when token1 is overwritten by move assignment.
                Seq(token1.Wait(),
                    [&wait1_resolved](bool success) {
                      wait1_resolved = true;
                      EXPECT_FALSE(success);
                      return absl::OkStatus();
                    }),
                // Promise 2: Wait on token2. It should resolve to `true`
                // when token1 (which now holds token2's latch) is completed.
                Seq(token2.Wait(),
                    [&wait2_resolved](bool success) {
                      wait2_resolved = true;
                      EXPECT_TRUE(success);
                      return absl::OkStatus();
                    }),
                // Promise 3: Trigger the move assignment and completion.
                // This runs during the first poll of the Join, triggering
                // immediate wakeups and resolution of the above two promises.
                [&token1, &token2]() {
                  token1 = std::move(token2);
                  token1.Complete(/*success=*/true);
                  return absl::OkStatus();
                }),
            // Discard the Join tuple result and return absl::Status.
            [](std::tuple<absl::Status, absl::Status, absl::Status>) {
              return absl::OkStatus();
            });
      },
      InlineWakeupScheduler(), [](absl::Status status) {});

  EXPECT_TRUE(wait1_resolved);
  EXPECT_TRUE(wait2_resolved);
}

TEST(ClientInitialMetadataOutstandingTokenTest, WaitAfterComplete) {
  if (!IsMetadataOutstandingTokenRefactorEnabled()) {
    GTEST_SKIP()
        << "Experiment client_initial_metadata_outstanding_token_refactor not "
           "enabled";
  }
  auto arena = Arena::Create(/*initial_size=*/1024, SimpleArenaAllocator());
  auto token = ClientInitialMetadataOutstandingToken::New(arena.get());

  // Complete the token first
  token.Complete(/*success=*/true);

  bool wait_resolved = false;
  auto activity = MakeActivity(
      [&token, &wait_resolved]() {
        return Seq(token.Wait(), [&wait_resolved](bool success) {
          wait_resolved = true;
          EXPECT_TRUE(success);
          return absl::OkStatus();
        });
      },
      InlineWakeupScheduler(), [](absl::Status) {});

  EXPECT_TRUE(wait_resolved);
}

TEST(ClientInitialMetadataOutstandingTokenTest, WaitOnEmptyToken) {
  if (!IsMetadataOutstandingTokenRefactorEnabled()) {
    GTEST_SKIP()
        << "Experiment client_initial_metadata_outstanding_token_refactor not "
           "enabled";
  }
  auto token = ClientInitialMetadataOutstandingToken::Empty();
  bool wait_resolved = false;
  auto activity = MakeActivity(
      [&token, &wait_resolved]() {
        return Seq(token.Wait(), [&wait_resolved](bool success) {
          wait_resolved = true;
          EXPECT_FALSE(success);
          return absl::OkStatus();
        });
      },
      InlineWakeupScheduler(), [](absl::Status) {});
  EXPECT_TRUE(wait_resolved);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
