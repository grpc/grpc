/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

DEFINE_string(generated_file_path, "",
              "path to the generated compiler_test.grpc.pb.h file");

const char kGoldenFilePath[] = "test/cpp/codegen/compiler_test_golden";

TEST(GoldenFileTest, TestGeneratedFile) {
  ASSERT_FALSE(FLAGS_generated_file_path.empty());

  std::ifstream generated(FLAGS_generated_file_path);
  std::ifstream golden(kGoldenFilePath);

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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
