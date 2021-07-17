//
// Copyright 2020 gRPC authors.
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

#include <stdio.h>
#include <string.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/stat.h"
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(STAT, GetTimestampOnTmpFile) {
  // Create a temporary empty file.
  FILE* tmp = nullptr;
  char* tmp_name;
  tmp = gpr_tmpfile("prefix", &tmp_name);
  ASSERT_NE(tmp_name, nullptr);
  ASSERT_NE(tmp, nullptr);
  fclose(tmp);
  // Check the last modified date is correctly set.
  time_t timestamp = 0;
  absl::Status status =
      grpc_core::GetFileModificationTime(tmp_name, &timestamp);
  EXPECT_EQ(status.code(), absl::StatusCode::kOk);
  EXPECT_GT(timestamp, 0);
  // Clean up.
  remove(tmp_name);
  gpr_free(tmp_name);
}

TEST(STAT, GetTimestampOnFailure) {
  time_t timestamp = 0;
  absl::Status status =
      grpc_core::GetFileModificationTime("/DOES_NOT_EXIST", &timestamp);
  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
  // Check the last modified date is not set.
  EXPECT_EQ(timestamp, 0);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
