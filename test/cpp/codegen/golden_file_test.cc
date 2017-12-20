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

#include <fstream>
#include <sstream>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

DEFINE_string(
    generated_file_path, "",
    "path to the directory containing generated files compiler_test.grpc.pb.h"
    "and compiler_test_mock.grpc.pb.h");

const char kGoldenFilePath[] = "test/cpp/codegen/compiler_test_golden";
const char kMockGoldenFilePath[] = "test/cpp/codegen/compiler_test_mock_golden";

void run_test(std::basic_string<char> generated_file,
              std::basic_string<char> golden_file) {
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
  run_test(FLAGS_generated_file_path + "compiler_test.grpc.pb.h",
           kGoldenFilePath);
}

TEST(GoldenMockFileTest, TestGeneratedMockFile) {
  run_test(FLAGS_generated_file_path + "compiler_test_mock.grpc.pb.h",
           kMockGoldenFilePath);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_generated_file_path.empty()) {
    FLAGS_generated_file_path = "gens/src/proto/grpc/testing/";
  }
  if (FLAGS_generated_file_path.back() != '/')
    FLAGS_generated_file_path.append("/");
  return RUN_ALL_TESTS();
}
