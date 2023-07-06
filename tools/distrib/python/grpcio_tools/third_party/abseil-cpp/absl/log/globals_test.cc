//
// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/log/globals.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/log/internal/globals.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"

namespace {

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

constexpr static absl::LogSeverityAtLeast DefaultMinLogLevel() {
  return absl::LogSeverityAtLeast::kInfo;
}
constexpr static absl::LogSeverityAtLeast DefaultStderrThreshold() {
  return absl::LogSeverityAtLeast::kError;
}

TEST(TestGlobals, MinLogLevel) {
  EXPECT_EQ(absl::MinLogLevel(), DefaultMinLogLevel());
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kError);
  EXPECT_EQ(absl::MinLogLevel(), absl::LogSeverityAtLeast::kError);
  absl::SetMinLogLevel(DefaultMinLogLevel());
}

TEST(TestGlobals, ScopedMinLogLevel) {
  EXPECT_EQ(absl::MinLogLevel(), DefaultMinLogLevel());
  {
    absl::log_internal::ScopedMinLogLevel scoped_stderr_threshold(
        absl::LogSeverityAtLeast::kError);
    EXPECT_EQ(absl::MinLogLevel(), absl::LogSeverityAtLeast::kError);
  }
  EXPECT_EQ(absl::MinLogLevel(), DefaultMinLogLevel());
}

TEST(TestGlobals, StderrThreshold) {
  EXPECT_EQ(absl::StderrThreshold(), DefaultStderrThreshold());
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kError);
  EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kError);
  absl::SetStderrThreshold(DefaultStderrThreshold());
}

TEST(TestGlobals, ScopedStderrThreshold) {
  EXPECT_EQ(absl::StderrThreshold(), DefaultStderrThreshold());
  {
    absl::ScopedStderrThreshold scoped_stderr_threshold(
        absl::LogSeverityAtLeast::kError);
    EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kError);
  }
  EXPECT_EQ(absl::StderrThreshold(), DefaultStderrThreshold());
}

TEST(TestGlobals, LogBacktraceAt) {
  EXPECT_FALSE(absl::log_internal::ShouldLogBacktraceAt("some_file.cc", 111));
  absl::SetLogBacktraceLocation("some_file.cc", 111);
  EXPECT_TRUE(absl::log_internal::ShouldLogBacktraceAt("some_file.cc", 111));
  EXPECT_FALSE(
      absl::log_internal::ShouldLogBacktraceAt("another_file.cc", 222));
}

TEST(TestGlobals, LogPrefix) {
  EXPECT_TRUE(absl::ShouldPrependLogPrefix());
  absl::EnableLogPrefix(false);
  EXPECT_FALSE(absl::ShouldPrependLogPrefix());
  absl::EnableLogPrefix(true);
  EXPECT_TRUE(absl::ShouldPrependLogPrefix());
}

}  // namespace
