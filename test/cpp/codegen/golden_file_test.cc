/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "absl/flags/flag.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(
    std::string, generated_file_path, "",
    "path to the directory containing generated files compiler_test.grpc.pb.h "
    "and compiler_test_mock.grpc.pb.h");

const char kGoldenFilePath[] = "test/cpp/codegen/compiler_test_golden";
const char kMockGoldenFilePath[] = "test/cpp/codegen/compiler_test_mock_golden";

void run_test(const std::basic_string<char>& generated_file,
              const std::basic_string<char>& golden_file) {
  std::ifstream generated(generated_file);
  std::ifstream golden(golden_file);

  ASSERT_TRUE(generated.good());
  ASSERT_TRUE(golden.good());

  std::ostringstream gen_oss;
  std::ostringstream gold_oss;
  gen_oss << generated.rdbuf();
  gold_oss << golden.rdbuf();
  EXPECT_EQ(gold_oss.str(), gen_oss.str());

  generated.close();
  golden.close();
}

TEST(GoldenFileTest, TestGeneratedFile) {
  run_test(absl::GetFlag(FLAGS_generated_file_path) + "compiler_test.grpc.pb.h",
           kGoldenFilePath);
}

TEST(GoldenMockFileTest, TestGeneratedMockFile) {
  run_test(
      absl::GetFlag(FLAGS_generated_file_path) + "compiler_test_mock.grpc.pb.h",
      kMockGoldenFilePath);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  if (absl::GetFlag(FLAGS_generated_file_path).empty()) {
    absl::SetFlag(&FLAGS_generated_file_path, "gens/src/proto/grpc/testing/");
  }
  if (absl::GetFlag(FLAGS_generated_file_path).back() != '/') {
    absl::SetFlag(&FLAGS_generated_file_path,
                  absl::GetFlag(FLAGS_generated_file_path).append("/"));
  }
  return RUN_ALL_TESTS();
}
