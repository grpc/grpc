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

#include <gtest/gtest.h>

// These paths rely on the fact that we run our tests under grpc/
const char kGeneratedFilePath[] =
    "gens/src/proto/grpc/testing/compiler_test.grpc.pb.h";
const char kGoldenFilePath[] = "test/cpp/codegen/compiler_test_golden";

TEST(GoldenFileTest, TestGeneratedFile) {
  std::ifstream generated(kGeneratedFilePath);
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
  return RUN_ALL_TESTS();
}
