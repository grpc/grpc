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
#include <grpc/support/port_platform.h>
#include <stdio.h>
#include <string.h>

#include <cstdint>
#include <string>

#if defined(GPR_POSIX_STAT) && defined(GPR_POSIX_TMPFILE)
#include <sys/stat.h>  // IWYU pragma: keep
#include <unistd.h>
#endif

#include "src/core/util/tmpfile.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

static const char prefix[] = "file_test";

TEST(LoadFileTest, TestLoadEmptyFile) {
  FILE* tmp = nullptr;
  absl::StatusOr<grpc_core::Slice> result;
  char* tmp_name;

  tmp = gpr_tmpfile(prefix, &tmp_name);
  ASSERT_NE(tmp_name, nullptr);
  ASSERT_NE(tmp, nullptr);
  fclose(tmp);

  result = grpc_core::LoadFile(tmp_name);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->length(), 0);

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

  result = grpc_core::LoadFile(tmp_name);
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

  result = grpc_core::LoadFile(tmp_name);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->length(), strlen(blah));
  ASSERT_FALSE(memcmp(result->begin(), blah, strlen(blah)));

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

  result = grpc_core::LoadFile(tmp_name);
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

TEST(LoadFileTest, TestLoadDirectory) {
  absl::StatusOr<grpc_core::Slice> result = grpc_core::LoadFile(".");
  ASSERT_FALSE(result.ok());
}

#if defined(GPR_POSIX_STAT) && defined(GPR_POSIX_TMPFILE)
namespace {

// Reserves a unique path in the platform temp directory without leaving a file
// behind so callers can create a directory or symlink there.
std::string ReserveTempPath() {
  char* tmp_name = nullptr;
  FILE* tmp = gpr_tmpfile(prefix, &tmp_name);
  if (tmp == nullptr || tmp_name == nullptr) {
    gpr_free(tmp_name);
    return "";
  }
  fclose(tmp);
  unlink(tmp_name);
  std::string path(tmp_name);
  gpr_free(tmp_name);
  return path;
}

}  // namespace

TEST(LoadFileTest, TestLoadDirectorySymlink) {
  const std::string dir_path = ReserveTempPath();
  const std::string link_path = ReserveTempPath();
  const std::string chained_link_path = ReserveTempPath();
  ASSERT_FALSE(dir_path.empty());
  ASSERT_FALSE(link_path.empty());
  ASSERT_FALSE(chained_link_path.empty());

  ASSERT_EQ(mkdir(dir_path.c_str(), 0700), 0);
  ASSERT_EQ(symlink(dir_path.c_str(), link_path.c_str()), 0);
  ASSERT_EQ(symlink(link_path.c_str(), chained_link_path.c_str()), 0);

  EXPECT_FALSE(grpc_core::LoadFile(dir_path).ok());
  EXPECT_FALSE(grpc_core::LoadFile(link_path).ok());
  EXPECT_FALSE(grpc_core::LoadFile(chained_link_path).ok());

  unlink(chained_link_path.c_str());
  unlink(link_path.c_str());
  rmdir(dir_path.c_str());
}

TEST(LoadFileTest, TestLoadFileViaDirectorySymlink) {
  const std::string dir_path = ReserveTempPath();
  const std::string dir_link_path = ReserveTempPath();
  const std::string file_link_path = ReserveTempPath();
  ASSERT_FALSE(dir_path.empty());
  ASSERT_FALSE(dir_link_path.empty());
  ASSERT_FALSE(file_link_path.empty());

  ASSERT_EQ(mkdir(dir_path.c_str(), 0700), 0);
  const std::string file_path = dir_path + "/payload";
  const char* blah = "blah";
  FILE* file = fopen(file_path.c_str(), "wb");
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(fwrite(blah, 1, strlen(blah), file), strlen(blah));
  fclose(file);

  const std::string file_via_dir_link = dir_link_path + "/payload";
  ASSERT_EQ(symlink(dir_path.c_str(), dir_link_path.c_str()), 0);
  ASSERT_EQ(symlink(file_via_dir_link.c_str(), file_link_path.c_str()), 0);

  EXPECT_FALSE(grpc_core::LoadFile(dir_link_path).ok());

  absl::StatusOr<grpc_core::Slice> via_dir_result =
      grpc_core::LoadFile(file_via_dir_link);
  ASSERT_TRUE(via_dir_result.ok());
  ASSERT_EQ(via_dir_result->length(), strlen(blah));
  EXPECT_EQ(memcmp(via_dir_result->begin(), blah, strlen(blah)), 0);

  absl::StatusOr<grpc_core::Slice> via_file_link_result =
      grpc_core::LoadFile(file_link_path);
  ASSERT_TRUE(via_file_link_result.ok());
  ASSERT_EQ(via_file_link_result->length(), strlen(blah));
  EXPECT_EQ(memcmp(via_file_link_result->begin(), blah, strlen(blah)), 0);

  unlink(file_link_path.c_str());
  unlink(dir_link_path.c_str());
  unlink(file_path.c_str());
  rmdir(dir_path.c_str());
}
#endif  // defined(GPR_POSIX_STAT) && defined(GPR_POSIX_TMPFILE)

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
