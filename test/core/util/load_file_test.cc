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

#include "src/core/util/load_file.h"

#include <grpc/support/alloc.h>
#include <stdio.h>
#include <string.h>

#include <cstdint>

#include "gtest/gtest.h"
#include "src/core/util/tmpfile.h"
#include "test/core/test_util/test_config.h"

static const char prefix[] = "file_test";

TEST(LoadFileTest, TestLoadEmptyFile) {
  FILE* tmp = nullptr;
  absl::StatusOr<grpc_core::Slice> result;
  char* tmp_name;

  tmp = gpr_tmpfile(prefix, &tmp_name);
  ASSERT_NE(tmp_name, nullptr);
  ASSERT_NE(tmp, nullptr);
  fclose(tmp);

  result = grpc_core::LoadFile(tmp_name, false);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->length(), 0);

  result = grpc_core::LoadFile(tmp_name, true);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->length(), 1);
  ASSERT_EQ(result->begin()[0], 0);

  remove(tmp_name);
  gpr_free(tmp_name);
}

TEST(LoadFileTest, TestLoadFailure) {
  FILE* tmp = nullptr;
  absl::StatusOr<grpc_core::Slice> result;
  char* tmp_name;

  tmp = gpr_tmpfile(prefix, &tmp_name);
  ASSERT_NE(tmp_name, nullptr);
  ASSERT_NE(tmp, nullptr);
  fclose(tmp);
  remove(tmp_name);

  result = grpc_core::LoadFile(tmp_name, false);
  ASSERT_FALSE(result.ok());

  gpr_free(tmp_name);
}

TEST(LoadFileTest, TestLoadSmallFile) {
  FILE* tmp = nullptr;
  absl::StatusOr<grpc_core::Slice> result;
  char* tmp_name;
  const char* blah = "blah";

  tmp = gpr_tmpfile(prefix, &tmp_name);
  ASSERT_NE(tmp_name, nullptr);
  ASSERT_NE(tmp, nullptr);
  ASSERT_EQ(fwrite(blah, 1, strlen(blah), tmp), strlen(blah));
  fclose(tmp);

  result = grpc_core::LoadFile(tmp_name, false);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->length(), strlen(blah));
  ASSERT_FALSE(memcmp(result->begin(), blah, strlen(blah)));

  result = grpc_core::LoadFile(tmp_name, true);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->length(), strlen(blah) + 1);
  ASSERT_STREQ(reinterpret_cast<const char*>(result->begin()), blah);

  remove(tmp_name);
  gpr_free(tmp_name);
}

TEST(LoadFileTest, TestLoadBigFile) {
  FILE* tmp = nullptr;
  absl::StatusOr<grpc_core::Slice> result;
  char* tmp_name;
  static const size_t buffer_size = 124631;
  unsigned char* buffer = static_cast<unsigned char*>(gpr_malloc(buffer_size));
  const uint8_t* current;
  size_t i;

  memset(buffer, 42, buffer_size);

  tmp = gpr_tmpfile(prefix, &tmp_name);
  ASSERT_NE(tmp, nullptr);
  ASSERT_NE(tmp_name, nullptr);
  ASSERT_EQ(fwrite(buffer, 1, buffer_size, tmp), buffer_size);
  fclose(tmp);

  result = grpc_core::LoadFile(tmp_name, false);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->length(), buffer_size);
  current = result->begin();
  for (i = 0; i < buffer_size; i++) {
    ASSERT_EQ(current[i], 42);
  }

  remove(tmp_name);
  gpr_free(tmp_name);
  gpr_free(buffer);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
