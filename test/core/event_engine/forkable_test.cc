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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/forkable.h"

#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/config/config_vars.h"

namespace {
using ::grpc_event_engine::experimental::Forkable;
using ::grpc_event_engine::experimental::RegisterForkHandlers;
}  // namespace

class ForkableTest : public testing::Test {};

TEST_F(ForkableTest, BasicPthreadAtForkOperations) {
  class SomeForkable : public Forkable {
   public:
    void PrepareFork() override { prepare_called_ = true; }
    void PostforkParent() override { parent_called_ = true; }
    void PostforkChild() override { child_called_ = true; }

    void CheckParent() {
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
      EXPECT_TRUE(prepare_called_);
      EXPECT_TRUE(parent_called_);
      EXPECT_FALSE(child_called_);
#else
      EXPECT_FALSE(prepare_called_);
      EXPECT_FALSE(parent_called_);
      EXPECT_FALSE(child_called_);
#endif
    }

    void CheckChild() {
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
      EXPECT_TRUE(prepare_called_);
      EXPECT_FALSE(parent_called_);
      EXPECT_TRUE(child_called_);
#else
      EXPECT_FALSE(prepare_called_);
      EXPECT_FALSE(parent_called_);
      EXPECT_FALSE(child_called_);
#endif
    }

   private:
    bool prepare_called_ = false;
    bool parent_called_ = false;
    bool child_called_ = false;
  };

  SomeForkable forkable;
  int child_pid = fork();
  ASSERT_NE(child_pid, -1);
  if (child_pid == 0) {
    gpr_log(GPR_DEBUG, "I am child pid: %d", getpid());
    forkable.CheckChild();
    exit(testing::Test::HasFailure());
  } else {
    gpr_log(GPR_DEBUG, "I am parent pid: %d", getpid());
    forkable.CheckParent();
    int status;
    gpr_log(GPR_DEBUG, "Waiting for child pid: %d", child_pid);
    do {
      // retry on EINTR, and fail otherwise
      if (waitpid(child_pid, &status, 0) != -1) break;
      ASSERT_EQ(errno, EINTR);
    } while (true);
    if (WIFEXITED(status)) {
      ASSERT_EQ(WEXITSTATUS(status), 0);
    } else {
      // exited abnormally, fail and print the exit status
      ASSERT_EQ(-99, status);
    }
  }
}

TEST_F(ForkableTest, NonPthreadManualForkOperations) {
  // Manually simulates a fork event for non-pthread-enabled environments
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  // This platform does not need to exercise fork support manually.
  GTEST_SKIP();
#endif

  class SomeForkable : public Forkable {
   public:
    void PrepareFork() override { prepare_called_ = true; }
    void PostforkParent() override { parent_called_ = true; }
    void PostforkChild() override { child_called_ = true; }

    void AssertStates(bool prepare, bool parent, bool child) {
      EXPECT_EQ(prepare_called_, prepare);
      EXPECT_EQ(parent_called_, parent);
      EXPECT_EQ(child_called_, child);
    }

   private:
    bool prepare_called_ = false;
    bool parent_called_ = false;
    bool child_called_ = false;
  };

  SomeForkable forkable;
  forkable.AssertStates(/*prepare=*/false, /*parent=*/false, /*child=*/false);
  grpc_event_engine::experimental::PrepareFork();
  forkable.AssertStates(/*prepare=*/true, /*parent=*/false, /*child=*/false);
  grpc_event_engine::experimental::PostforkParent();
  forkable.AssertStates(/*prepare=*/true, /*parent=*/true, /*child=*/false);
  grpc_event_engine::experimental::PostforkChild();
  forkable.AssertStates(/*prepare=*/true, /*parent=*/true, /*child=*/true);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  // Force enable fork support to allow testing the fork handler registry.
  grpc_core::ConfigVars::Overrides config_overrides;
  config_overrides.enable_fork_support = true;
  grpc_core::ConfigVars::SetOverrides(config_overrides);
  RegisterForkHandlers();
  auto result = RUN_ALL_TESTS();
  return result;
}
