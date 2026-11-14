//
// Copyright 2023 gRPC authors.
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

#include "src/core/util/directory_reader.h"

#ifndef _WIN32
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <string>
#include <vector>

#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

static constexpr absl::string_view kCrlDirectory =
    "test/core/tsi/test_creds/crl_data/crls/";

namespace grpc_core {
namespace testing {
namespace {

TEST(DirectoryReader, CanListFiles) {
  auto reader = MakeDirectoryReader(kCrlDirectory);
  std::vector<std::string> contents;
  absl::Status status = reader->ForEach([&](absl::string_view filename) {
    contents.push_back(std::string(filename));
  });
  ASSERT_TRUE(status.ok()) << status;
  // IsSupersetOf() is needed instead of UnorderedElementsAre() because some
  // builds/OS combinations will include the BUILD file in this directory when
  // the tests are run
  EXPECT_THAT(contents,
              ::testing::IsSupersetOf({"ab06acdd.r0", "b9322cac.r0",
                                       "current.crl", "intermediate.crl"}));
}

TEST(DirectoryReader, NonexistentDirectory) {
  auto reader = MakeDirectoryReader("DOES_NOT_EXIST");
  absl::Status status = reader->ForEach([](absl::string_view) {});
  ASSERT_FALSE(status.ok()) << status;
}

#ifndef _WIN32
TEST(DirectoryReader, SkipsSubdirectoriesAndDirectorySymlinksButFollowsFiles) {
  const std::string base_dir =
      absl::StrCat(::testing::TempDir(), "/directory_reader_symlink_test");
  const std::string crl_dir = absl::StrCat(base_dir, "/crl");
  const std::string sub_dir = absl::StrCat(crl_dir, "/nested_subdir");
  const std::string sym_target_dir = absl::StrCat(base_dir, "/sym_target");
  ASSERT_EQ(mkdir(base_dir.c_str(), 0700), 0);
  ASSERT_EQ(mkdir(crl_dir.c_str(), 0700), 0);
  ASSERT_EQ(mkdir(sub_dir.c_str(), 0700), 0);
  ASSERT_EQ(mkdir(sym_target_dir.c_str(), 0700), 0);

  // Regular file directly in crl_dir.
  const std::string direct_file = absl::StrCat(crl_dir, "/direct.r0");
  FILE* f1 = fopen(direct_file.c_str(), "wb");
  ASSERT_NE(f1, nullptr);
  fclose(f1);

  // Target file in sym_target_dir, exposed via a directory symlink `sym` and a
  // relative file symlink `linked.r0 -> sym/target.r0`.
  const std::string target_file = absl::StrCat(sym_target_dir, "/target.r0");
  FILE* f2 = fopen(target_file.c_str(), "wb");
  ASSERT_NE(f2, nullptr);
  fclose(f2);
  ASSERT_EQ(
      symlink(sym_target_dir.c_str(), absl::StrCat(crl_dir, "/sym").c_str()),
      0);
  ASSERT_EQ(
      symlink("sym/target.r0", absl::StrCat(crl_dir, "/linked.r0").c_str()),
      0);
  ASSERT_EQ(symlink("sym/missing.r0",
                    absl::StrCat(crl_dir, "/dangling.r0").c_str()),
            0);

  auto reader = MakeDirectoryReader(crl_dir);
  std::vector<std::string> contents;
  absl::Status status = reader->ForEach([&](absl::string_view filename) {
    contents.push_back(std::string(filename));
  });
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_THAT(contents,
              ::testing::UnorderedElementsAre("direct.r0", "linked.r0"));
}
#endif

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
