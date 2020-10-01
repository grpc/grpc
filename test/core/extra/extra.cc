/*
 *
 * Copyright 2020 gRPC authors.
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

#include <stdio.h>
#include <string.h>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"

namespace {

std::string get_trace() {
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


static constexpr int kPrintfPointerFieldWidth = 2 + 2 * sizeof(void*);

static void DumpPCAndFrameSizeAndSymbol(void (*writerfn)(const char*, void*),
                                        void* writerfn_arg, void* pc,
                                        void* symbolize_pc, int framesize,
                                        const char* const prefix) {
  char tmp[1024];
  const char* symbol = "(unknown)";
  if (absl::Symbolize(symbolize_pc, tmp, sizeof(tmp))) {
    symbol = tmp;
  }
  char buf[1024];
  if (framesize <= 0) {
    snprintf(buf, sizeof(buf), "%s@ %*p  (unknown)  %s\n", prefix,
             kPrintfPointerFieldWidth, pc, symbol);
  } else {
    snprintf(buf, sizeof(buf), "%s@ %*p  %9d  %s\n", prefix,
             kPrintfPointerFieldWidth, pc, framesize, symbol);
  }
  writerfn(buf, writerfn_arg);
}

static void DumpPCAndFrameSize(void (*writerfn)(const char*, void*),
                               void* writerfn_arg, void* pc, int framesize,
                               const char* const prefix) {
  char buf[100];
  if (framesize <= 0) {
    snprintf(buf, sizeof(buf), "%s@ %*p  (unknown)\n", prefix,
             kPrintfPointerFieldWidth, pc);
  } else {
    snprintf(buf, sizeof(buf), "%s@ %*p  %9d\n", prefix,
             kPrintfPointerFieldWidth, pc, framesize);
  }
  writerfn(buf, writerfn_arg);
}

static void DumpStackTrace(void* const stack[], int frame_sizes[], int depth,
                           bool symbolize_stacktrace,
                           void (*writerfn)(const char*, void*),
                           void* writerfn_arg) {
  for (int i = 0; i < depth; i++) {
    if (symbolize_stacktrace) {
      DumpPCAndFrameSizeAndSymbol(writerfn, writerfn_arg, stack[i],
                                  reinterpret_cast<char*>(stack[i]) - 1,
                                  frame_sizes[i], "    ");
    } else {
      DumpPCAndFrameSize(writerfn, writerfn_arg, stack[i], frame_sizes[i],
                         "    ");
    }
  }
}

static void DebugWriteToString(const char* data, void* str) {
  reinterpret_cast<std::string*>(str)->append(data);
}

std::string CurrentStackTrace() {
  std::string result = "Stack trace:\n";
  constexpr int kNumStackFrames = 32;
  void* stack[kNumStackFrames];
  int frame_sizes[kNumStackFrames];
  int depth = absl::GetStackFrames(stack, frame_sizes, kNumStackFrames, 1);
  DumpStackTrace(stack, frame_sizes, depth, true, DebugWriteToString,
                 (void*)&result);
  return result;
}

}  // namespace

TEST(ExtraTest, Basic) {
  std::string st = CurrentStackTrace();
  fprintf(stderr, "get_trace()=[%s]\n", st.c_str());
  EXPECT_TRUE(st.find("Basic") != -1);
}

TEST(ExtraTest, MultiThreading) {
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; i++) {
    threads.push_back(std::thread([]() {
      for (int j = 0; j < 1000; j++) { CurrentStackTrace(); }
    }));
  }
  for (int i = 0; i < threads.size(); i++) threads[i].join();
}

int main(int argc, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}