//
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
//

#include "src/core/util/examine_stack.h"

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/log.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

std::string SimpleCurrentStackTraceProvider() { return "stacktrace"; }

std::string AbseilCurrentStackTraceProvider() {
  std::string result = "Stack trace:\n";
  constexpr int kNumStackFrames = 10;
  void* stack[kNumStackFrames];
  int frame_sizes[kNumStackFrames];
  int depth = absl::GetStackFrames(stack, frame_sizes, kNumStackFrames, 1);
  for (int i = 0; i < depth; i++) {
    char tmp[1024];
    const char* symbol = "(unknown)";
    if (absl::Symbolize(stack[i], tmp, sizeof(tmp))) {
      symbol = tmp;
    }
    result += symbol;
    result += +"\n";
  }
  return result;
}

}  // namespace

TEST(ExamineStackTest, NullStackProvider) {
  grpc_core::SetCurrentStackTraceProvider(nullptr);
  EXPECT_EQ(grpc_core::GetCurrentStackTraceProvider(), nullptr);
  EXPECT_EQ(grpc_core::GetCurrentStackTrace(), std::nullopt);
}

TEST(ExamineStackTest, SimpleStackProvider) {
  grpc_core::SetCurrentStackTraceProvider(&SimpleCurrentStackTraceProvider);
  EXPECT_NE(grpc_core::GetCurrentStackTraceProvider(), nullptr);
  EXPECT_EQ(grpc_core::GetCurrentStackTrace(), "stacktrace");
}

TEST(ExamineStackTest, AbseilStackProvider) {
  grpc_core::SetCurrentStackTraceProvider(&AbseilCurrentStackTraceProvider);
  EXPECT_NE(grpc_core::GetCurrentStackTraceProvider(), nullptr);
  const std::optional<std::string> stack_trace =
      grpc_core::GetCurrentStackTrace();
  EXPECT_NE(stack_trace, std::nullopt);
  LOG(INFO) << "stack_trace=" << *stack_trace;
#if !defined(NDEBUG) && !defined(GPR_MUSL_LIBC_COMPAT)
  // Expect to see some gtest signature on the stack (this used to be
  // GetCurrentStackTrace, but some operating systems have trouble with the leaf
  // function).
  EXPECT_THAT(*stack_trace, ::testing::HasSubstr("testing::"));
#endif
}

int main(int argc, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
