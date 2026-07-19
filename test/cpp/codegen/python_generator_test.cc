//
//
// Copyright 2026 gRPC authors.
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
//

#include "src/compiler/python_generator.h"

#include <string>

#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

namespace grpc_python_generator {
namespace {

TEST(EscapePythonDocstringTest, PlainTextUnchanged) {
  EXPECT_EQ("Hello world", EscapePythonDocstring("Hello world"));
}

TEST(EscapePythonDocstringTest, EmptyString) {
  EXPECT_EQ("", EscapePythonDocstring(""));
}

TEST(EscapePythonDocstringTest, SingleBackslashEscaped) {
  EXPECT_EQ("path\\\\to\\\\file", EscapePythonDocstring("path\\to\\file"));
}

TEST(EscapePythonDocstringTest, TripleQuoteEscaped) {
  // A triple-quote sequence in the input should be escaped so it does not
  // terminate the docstring in generated Python code.
  EXPECT_EQ("before\\\"\\\"\\\"after",
            EscapePythonDocstring("before\"\"\"after"));
}

TEST(EscapePythonDocstringTest, TripleQuoteAtStart) {
  EXPECT_EQ("\\\"\\\"\\\"start", EscapePythonDocstring("\"\"\"start"));
}

TEST(EscapePythonDocstringTest, TripleQuoteAtEnd) {
  EXPECT_EQ("end\\\"\\\"\\\"", EscapePythonDocstring("end\"\"\""));
}

TEST(EscapePythonDocstringTest, ConsecutiveTripleQuotes) {
  // Six quotes = two triple-quote sequences
  EXPECT_EQ("\\\"\\\"\\\"\\\"\\\"\\\"",
            EscapePythonDocstring("\"\"\"\"\"\""));
}

TEST(EscapePythonDocstringTest, DoubleQuoteNotEscaped) {
  // A single double-quote or two double-quotes should not be affected.
  EXPECT_EQ("say \"hello\"", EscapePythonDocstring("say \"hello\""));
  EXPECT_EQ("two \"\" quotes", EscapePythonDocstring("two \"\" quotes"));
}

TEST(EscapePythonDocstringTest, BackslashBeforeTripleQuote) {
  // Backslash immediately before triple-quote: both should be escaped.
  EXPECT_EQ("\\\\\\\"\\\"\\\"", EscapePythonDocstring("\\\"\"\""));
}

TEST(EscapePythonDocstringTest, MaliciousInjection) {
  // Simulates a proto comment designed to break out of the docstring and
  // inject arbitrary Python code.
  std::string malicious = "\"\"\"\nimport os; os.system('rm -rf /')\n\"\"\"";
  std::string escaped = EscapePythonDocstring(malicious);
  // The escaped output must not contain an unescaped triple-quote sequence.
  EXPECT_EQ(std::string::npos, escaped.find("\"\"\""));
}

}  // namespace
}  // namespace grpc_python_generator

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
