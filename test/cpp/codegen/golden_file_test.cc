/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <fstream>
#include <sstream>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::google::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_generated_file_path.empty()) {
    FLAGS_generated_file_path = "gens/src/proto/grpc/testing/";
  }
  if (FLAGS_generated_file_path.back() != '/')
    FLAGS_generated_file_path.append("/");
  return RUN_ALL_TESTS();
}
