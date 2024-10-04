// Copyright 2024 The gRPC Authors
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

#include <grpc/grpc.h>

#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "src/core/lib/debug/trace.h"
#include "test/core/test_util/test_config.h"

class TraceFlags : public ::testing::Test {
 public:
  TraceFlags() {}
  ~TraceFlags() override { saved_flags_.Restore(); }

 private:
  grpc_core::SavedTraceFlags saved_flags_;
};

TEST_F(TraceFlags, ReturnsFalseWhenNotFound) {
  ASSERT_FALSE(grpc_core::ParseTracers("arst"));
}

TEST_F(TraceFlags, CanSetAValue) {
  auto it = grpc_core::GetAllTraceFlags().find("api");
  ASSERT_NE(it, grpc_core::GetAllTraceFlags().end());
  EXPECT_TRUE(grpc_core::ParseTracers("api"));
  EXPECT_TRUE(it->second->enabled());
  EXPECT_TRUE(grpc_core::ParseTracers("-api"));
  EXPECT_FALSE(it->second->enabled());
}

TEST_F(TraceFlags, SpecialRefcountFlagWorks) {
  for (const auto& flag : grpc_core::GetAllTraceFlags()) {
    if (absl::StrContains(flag.first, "refcount")) {
      EXPECT_FALSE(flag.second->enabled());
    }
  }
  EXPECT_TRUE(grpc_core::ParseTracers("refcount"));
  for (const auto& flag : grpc_core::GetAllTraceFlags()) {
    if (absl::StrContains(flag.first, "refcount")) {
      EXPECT_TRUE(flag.second->enabled());
    }
  }
}

TEST_F(TraceFlags, SpecialAllFlagWorks) {
  // Snapshot the trace flag state at the beginning
  EXPECT_TRUE(grpc_core::ParseTracers("all"));
  for (const auto& flag : grpc_core::GetAllTraceFlags()) {
    EXPECT_TRUE(flag.second->enabled()) << flag.first << " was not enabled.";
  }
  EXPECT_TRUE(grpc_core::ParseTracers("-all"));
  for (const auto& flag : grpc_core::GetAllTraceFlags()) {
    EXPECT_FALSE(flag.second->enabled())
        << flag.first << " was still enabled after turning all flags off.";
  }
}

TEST_F(TraceFlags, GlobStarAllWorks) {
  // Snapshot the trace flag state at the beginning
  EXPECT_TRUE(grpc_core::ParseTracers("*"));
  for (const auto& flag : grpc_core::GetAllTraceFlags()) {
    EXPECT_TRUE(flag.second->enabled()) << flag.first << " was not enabled.";
  }
  EXPECT_TRUE(grpc_core::ParseTracers("-*"));
  for (const auto& flag : grpc_core::GetAllTraceFlags()) {
    EXPECT_FALSE(flag.second->enabled())
        << flag.first << " was still enabled after turning all flags off.";
  }
}

TEST_F(TraceFlags, SaveAndRestoreWorks) {
  auto it = grpc_core::GetAllTraceFlags().find("api");
  ASSERT_NE(it, grpc_core::GetAllTraceFlags().end());
  EXPECT_FALSE(it->second->enabled());
  grpc_core::SavedTraceFlags saved_1;
  EXPECT_TRUE(grpc_core::ParseTracers("all"));
  EXPECT_TRUE(it->second->enabled());
  saved_1.Restore();
  EXPECT_FALSE(it->second->enabled());
}

TEST_F(TraceFlags, CApiStillWorks) {
  auto it = grpc_core::GetAllTraceFlags().find("api");
  ASSERT_NE(it, grpc_core::GetAllTraceFlags().end());
  EXPECT_FALSE(it->second->enabled());
  EXPECT_TRUE(grpc_tracer_set_enabled("all", true));
  EXPECT_TRUE(it->second->enabled());
  EXPECT_TRUE(grpc_tracer_set_enabled("all", false));
  EXPECT_FALSE(it->second->enabled());
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  auto res = RUN_ALL_TESTS();
  grpc_shutdown();
  return res;
}
