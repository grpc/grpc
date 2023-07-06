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

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/atomic_hook.h"
#include "absl/base/log_severity.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

// These atomics represent logging library configuration.
// Integer types are used instead of absl::LogSeverity to ensure that a
// lock-free std::atomic is used when possible.
ABSL_CONST_INIT std::atomic<int> min_log_level{
    static_cast<int>(absl::LogSeverityAtLeast::kInfo)};
ABSL_CONST_INIT std::atomic<int> stderrthreshold{
    static_cast<int>(absl::LogSeverityAtLeast::kError)};
// We evaluate this value as a hash comparison to avoid having to
// hold a mutex or make a copy (to access the value of a string-typed flag) in
// very hot codepath.
ABSL_CONST_INIT std::atomic<size_t> log_backtrace_at_hash{0};
ABSL_CONST_INIT std::atomic<bool> prepend_log_prefix{true};

ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES
absl::base_internal::AtomicHook<log_internal::LoggingGlobalsListener>
    logging_globals_listener;

size_t HashSiteForLogBacktraceAt(absl::string_view file, int line) {
  return absl::HashOf(file, line);
}

void TriggerLoggingGlobalsListener() {
  auto* listener = logging_globals_listener.Load();
  if (listener != nullptr) listener();
}

}  // namespace

namespace log_internal {

void RawSetMinLogLevel(absl::LogSeverityAtLeast severity) {
  min_log_level.store(static_cast<int>(severity), std::memory_order_release);
}

void RawSetStderrThreshold(absl::LogSeverityAtLeast severity) {
  stderrthreshold.store(static_cast<int>(severity), std::memory_order_release);
}

void RawEnableLogPrefix(bool on_off) {
  prepend_log_prefix.store(on_off, std::memory_order_release);
}

void SetLoggingGlobalsListener(LoggingGlobalsListener l) {
  logging_globals_listener.Store(l);
}

}  // namespace log_internal

absl::LogSeverityAtLeast MinLogLevel() {
  return static_cast<absl::LogSeverityAtLeast>(
      min_log_level.load(std::memory_order_acquire));
}

void SetMinLogLevel(absl::LogSeverityAtLeast severity) {
  log_internal::RawSetMinLogLevel(severity);
  TriggerLoggingGlobalsListener();
}

namespace log_internal {

ScopedMinLogLevel::ScopedMinLogLevel(absl::LogSeverityAtLeast severity)
    : saved_severity_(absl::MinLogLevel()) {
  absl::SetMinLogLevel(severity);
}
ScopedMinLogLevel::~ScopedMinLogLevel() {
  absl::SetMinLogLevel(saved_severity_);
}

}  // namespace log_internal

absl::LogSeverityAtLeast StderrThreshold() {
  return static_cast<absl::LogSeverityAtLeast>(
      stderrthreshold.load(std::memory_order_acquire));
}

void SetStderrThreshold(absl::LogSeverityAtLeast severity) {
  log_internal::RawSetStderrThreshold(severity);
  TriggerLoggingGlobalsListener();
}

ScopedStderrThreshold::ScopedStderrThreshold(absl::LogSeverityAtLeast severity)
    : saved_severity_(absl::StderrThreshold()) {
  absl::SetStderrThreshold(severity);
}

ScopedStderrThreshold::~ScopedStderrThreshold() {
  absl::SetStderrThreshold(saved_severity_);
}

namespace log_internal {

bool ShouldLogBacktraceAt(absl::string_view file, int line) {
  const size_t flag_hash =
      log_backtrace_at_hash.load(std::memory_order_acquire);

  return flag_hash != 0 && flag_hash == HashSiteForLogBacktraceAt(file, line);
}

}  // namespace log_internal

void SetLogBacktraceLocation(absl::string_view file, int line) {
  log_backtrace_at_hash.store(HashSiteForLogBacktraceAt(file, line),
                              std::memory_order_release);
}

bool ShouldPrependLogPrefix() {
  return prepend_log_prefix.load(std::memory_order_acquire);
}

void EnableLogPrefix(bool on_off) {
  log_internal::RawEnableLogPrefix(on_off);
  TriggerLoggingGlobalsListener();
}

ABSL_NAMESPACE_END
}  // namespace absl
