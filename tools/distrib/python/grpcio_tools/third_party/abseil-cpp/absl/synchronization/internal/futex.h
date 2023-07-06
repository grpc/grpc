// Copyright 2020 The Abseil Authors.
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
#ifndef ABSL_SYNCHRONIZATION_INTERNAL_FUTEX_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_FUTEX_H_

#include "absl/base/config.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <atomic>
#include <cstdint>

#include "absl/base/optimization.h"
#include "absl/synchronization/internal/kernel_timeout.h"

#ifdef ABSL_INTERNAL_HAVE_FUTEX
#error ABSL_INTERNAL_HAVE_FUTEX may not be set on the command line
#elif defined(__BIONIC__)
// Bionic supports all the futex operations we need even when some of the futex
// definitions are missing.
#define ABSL_INTERNAL_HAVE_FUTEX
#elif defined(__linux__) && defined(FUTEX_CLOCK_REALTIME)
// FUTEX_CLOCK_REALTIME requires Linux >= 2.6.28.
#define ABSL_INTERNAL_HAVE_FUTEX
#endif

#ifdef ABSL_INTERNAL_HAVE_FUTEX

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

// Some Android headers are missing these definitions even though they
// support these futex operations.
#ifdef __BIONIC__
#ifndef SYS_futex
#define SYS_futex __NR_futex
#endif
#ifndef FUTEX_WAIT_BITSET
#define FUTEX_WAIT_BITSET 9
#endif
#ifndef FUTEX_PRIVATE_FLAG
#define FUTEX_PRIVATE_FLAG 128
#endif
#ifndef FUTEX_CLOCK_REALTIME
#define FUTEX_CLOCK_REALTIME 256
#endif
#ifndef FUTEX_BITSET_MATCH_ANY
#define FUTEX_BITSET_MATCH_ANY 0xFFFFFFFF
#endif
#endif

#if defined(__NR_futex_time64) && !defined(SYS_futex_time64)
#define SYS_futex_time64 __NR_futex_time64
#endif

#if defined(SYS_futex_time64) && !defined(SYS_futex)
#define SYS_futex SYS_futex_time64
#endif

class FutexImpl {
 public:
  static int WaitUntil(std::atomic<int32_t> *v, int32_t val,
                       KernelTimeout t) {
    long err = 0;  // NOLINT(runtime/int)
    if (t.has_timeout()) {
      // https://locklessinc.com/articles/futex_cheat_sheet/
      // Unlike FUTEX_WAIT, FUTEX_WAIT_BITSET uses absolute time.
      struct timespec abs_timeout = t.MakeAbsTimespec();
      // Atomically check that the futex value is still 0, and if it
      // is, sleep until abs_timeout or until woken by FUTEX_WAKE.
      err = syscall(
          SYS_futex, reinterpret_cast<int32_t *>(v),
          FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME, val,
          &abs_timeout, nullptr, FUTEX_BITSET_MATCH_ANY);
    } else {
      // Atomically check that the futex value is still 0, and if it
      // is, sleep until woken by FUTEX_WAKE.
      err = syscall(SYS_futex, reinterpret_cast<int32_t *>(v),
                    FUTEX_WAIT | FUTEX_PRIVATE_FLAG, val, nullptr);
    }
    if (ABSL_PREDICT_FALSE(err != 0)) {
      return -errno;
    }
    return 0;
  }

  static int WaitBitsetAbsoluteTimeout(std::atomic<int32_t> *v, int32_t val,
                                       int32_t bits,
                                       const struct timespec *abstime) {
    // NOLINTNEXTLINE(runtime/int)
    long err = syscall(SYS_futex, reinterpret_cast<int32_t*>(v),
                       FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG, val, abstime,
                       nullptr, bits);
    if (ABSL_PREDICT_FALSE(err != 0)) {
      return -errno;
    }
    return 0;
  }

  static int Wake(std::atomic<int32_t> *v, int32_t count) {
    // NOLINTNEXTLINE(runtime/int)
    long err = syscall(SYS_futex, reinterpret_cast<int32_t*>(v),
                       FUTEX_WAKE | FUTEX_PRIVATE_FLAG, count);
    if (ABSL_PREDICT_FALSE(err < 0)) {
      return -errno;
    }
    return 0;
  }

  // FUTEX_WAKE_BITSET
  static int WakeBitset(std::atomic<int32_t> *v, int32_t count, int32_t bits) {
    // NOLINTNEXTLINE(runtime/int)
    long err = syscall(SYS_futex, reinterpret_cast<int32_t*>(v),
                       FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG, count, nullptr,
                       nullptr, bits);
    if (ABSL_PREDICT_FALSE(err < 0)) {
      return -errno;
    }
    return 0;
  }
};

class Futex : public FutexImpl {};

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_FUTEX

#endif  // ABSL_SYNCHRONIZATION_INTERNAL_FUTEX_H_
