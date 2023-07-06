// Copyright 2017 The Abseil Authors.
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
//

// An optional absolute timeout, with nanosecond granularity,
// compatible with absl::Time. Suitable for in-register
// parameter-passing (e.g. syscalls.)
// Constructible from a absl::Time (for a timeout to be respected) or {}
// (for "no timeout".)
// This is a private low-level API for use by a handful of low-level
// components. Higher-level components should build APIs based on
// absl::Time and absl::Duration.

#ifndef ABSL_SYNCHRONIZATION_INTERNAL_KERNEL_TIMEOUT_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_KERNEL_TIMEOUT_H_

#include <time.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include "absl/base/internal/raw_logging.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

class Waiter;

class KernelTimeout {
 public:
  // A timeout that should expire at <t>.  Any value, in the full
  // InfinitePast() to InfiniteFuture() range, is valid here and will be
  // respected.
  explicit KernelTimeout(absl::Time t) : ns_(MakeNs(t)) {}
  // No timeout.
  KernelTimeout() : ns_(0) {}

  // A more explicit factory for those who prefer it.  Equivalent to {}.
  static KernelTimeout Never() { return {}; }

  // We explicitly do not support other custom formats: timespec, int64_t nanos.
  // Unify on this and absl::Time, please.

  bool has_timeout() const { return ns_ != 0; }

  // Convert to parameter for sem_timedwait/futex/similar.  Only for approved
  // users.  Do not call if !has_timeout.
  struct timespec MakeAbsTimespec() const;

  // Convert to unix epoch nanos.  Do not call if !has_timeout.
  int64_t MakeAbsNanos() const;

 private:
  // internal rep, not user visible: ns after unix epoch.
  // zero = no timeout.
  // Negative we treat as an unlikely (and certainly expired!) but valid
  // timeout.
  int64_t ns_;

  static int64_t MakeNs(absl::Time t) {
    // optimization--InfiniteFuture is common "no timeout" value
    // and cheaper to compare than convert.
    if (t == absl::InfiniteFuture()) return 0;
    int64_t x = ToUnixNanos(t);

    // A timeout that lands exactly on the epoch (x=0) needs to be respected,
    // so we alter it unnoticably to 1.  Negative timeouts are in
    // theory supported, but handled poorly by the kernel (long
    // delays) so push them forward too; since all such times have
    // already passed, it's indistinguishable.
    if (x <= 0) x = 1;
    // A time larger than what can be represented to the kernel is treated
    // as no timeout.
    if (x == (std::numeric_limits<int64_t>::max)()) x = 0;
    return x;
  }

#ifdef _WIN32
  // Converts to milliseconds from now, or INFINITE when
  // !has_timeout(). For use by SleepConditionVariableSRW on
  // Windows. Callers should recognize that the return value is a
  // relative duration (it should be recomputed by calling this method
  // in the case of a spurious wakeup).
  // This header file may be included transitively by public header files,
  // so we define our own DWORD and INFINITE instead of getting them from
  // <intsafe.h> and <WinBase.h>.
  typedef unsigned long DWord;  // NOLINT
  DWord InMillisecondsFromNow() const {
    constexpr DWord kInfinite = (std::numeric_limits<DWord>::max)();
    if (!has_timeout()) {
      return kInfinite;
    }
    // The use of absl::Now() to convert from absolute time to
    // relative time means that absl::Now() cannot use anything that
    // depends on KernelTimeout (for example, Mutex) on Windows.
    int64_t now = ToUnixNanos(absl::Now());
    if (ns_ >= now) {
      // Round up so that Now() + ms_from_now >= ns_.
      constexpr uint64_t max_nanos =
          (std::numeric_limits<int64_t>::max)() - 999999u;
      uint64_t ms_from_now =
          ((std::min)(max_nanos, static_cast<uint64_t>(ns_ - now)) + 999999u) /
          1000000u;
      if (ms_from_now > kInfinite) {
        return kInfinite;
      }
      return static_cast<DWord>(ms_from_now);
    }
    return 0;
  }

  friend class Waiter;
#endif
};

inline struct timespec KernelTimeout::MakeAbsTimespec() const {
  int64_t n = ns_;
  static const int64_t kNanosPerSecond = 1000 * 1000 * 1000;
  if (n == 0) {
    ABSL_RAW_LOG(
        ERROR, "Tried to create a timespec from a non-timeout; never do this.");
    // But we'll try to continue sanely.  no-timeout ~= saturated timeout.
    n = (std::numeric_limits<int64_t>::max)();
  }

  // Kernel APIs validate timespecs as being at or after the epoch,
  // despite the kernel time type being signed.  However, no one can
  // tell the difference between a timeout at or before the epoch (since
  // all such timeouts have expired!)
  if (n < 0) n = 0;

  struct timespec abstime;
  int64_t seconds = (std::min)(n / kNanosPerSecond,
                               int64_t{(std::numeric_limits<time_t>::max)()});
  abstime.tv_sec = static_cast<time_t>(seconds);
  abstime.tv_nsec = static_cast<decltype(abstime.tv_nsec)>(n % kNanosPerSecond);
  return abstime;
}

inline int64_t KernelTimeout::MakeAbsNanos() const {
  if (ns_ == 0) {
    ABSL_RAW_LOG(
        ERROR, "Tried to create a timeout from a non-timeout; never do this.");
    // But we'll try to continue sanely.  no-timeout ~= saturated timeout.
    return (std::numeric_limits<int64_t>::max)();
  }

  return ns_;
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_SYNCHRONIZATION_INTERNAL_KERNEL_TIMEOUT_H_
