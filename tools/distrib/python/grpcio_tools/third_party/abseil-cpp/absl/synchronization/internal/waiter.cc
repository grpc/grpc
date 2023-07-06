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

#include "absl/synchronization/internal/waiter.h"

#include "absl/base/config.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

#ifdef ABSL_HAVE_SEMAPHORE_H
#include <semaphore.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <new>
#include <type_traits>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/optimization.h"
#include "absl/synchronization/internal/kernel_timeout.h"


namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

static void MaybeBecomeIdle() {
  base_internal::ThreadIdentity *identity =
      base_internal::CurrentThreadIdentityIfPresent();
  assert(identity != nullptr);
  const bool is_idle = identity->is_idle.load(std::memory_order_relaxed);
  const int ticker = identity->ticker.load(std::memory_order_relaxed);
  const int wait_start = identity->wait_start.load(std::memory_order_relaxed);
  if (!is_idle && ticker - wait_start > Waiter::kIdlePeriods) {
    identity->is_idle.store(true, std::memory_order_relaxed);
  }
}

#if ABSL_WAITER_MODE == ABSL_WAITER_MODE_FUTEX

Waiter::Waiter() {
  futex_.store(0, std::memory_order_relaxed);
}

bool Waiter::Wait(KernelTimeout t) {
  // Loop until we can atomically decrement futex from a positive
  // value, waiting on a futex while we believe it is zero.
  // Note that, since the thread ticker is just reset, we don't need to check
  // whether the thread is idle on the very first pass of the loop.
  bool first_pass = true;

  while (true) {
    int32_t x = futex_.load(std::memory_order_relaxed);
    while (x != 0) {
      if (!futex_.compare_exchange_weak(x, x - 1,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed)) {
        continue;  // Raced with someone, retry.
      }
      return true;  // Consumed a wakeup, we are done.
    }

    if (!first_pass) MaybeBecomeIdle();
    const int err = Futex::WaitUntil(&futex_, 0, t);
    if (err != 0) {
      if (err == -EINTR || err == -EWOULDBLOCK) {
        // Do nothing, the loop will retry.
      } else if (err == -ETIMEDOUT) {
        return false;
      } else {
        ABSL_RAW_LOG(FATAL, "Futex operation failed with error %d\n", err);
      }
    }
    first_pass = false;
  }
}

void Waiter::Post() {
  if (futex_.fetch_add(1, std::memory_order_release) == 0) {
    // We incremented from 0, need to wake a potential waiter.
    Poke();
  }
}

void Waiter::Poke() {
  // Wake one thread waiting on the futex.
  const int err = Futex::Wake(&futex_, 1);
  if (ABSL_PREDICT_FALSE(err < 0)) {
    ABSL_RAW_LOG(FATAL, "Futex operation failed with error %d\n", err);
  }
}

#elif ABSL_WAITER_MODE == ABSL_WAITER_MODE_CONDVAR

class PthreadMutexHolder {
 public:
  explicit PthreadMutexHolder(pthread_mutex_t *mu) : mu_(mu) {
    const int err = pthread_mutex_lock(mu_);
    if (err != 0) {
      ABSL_RAW_LOG(FATAL, "pthread_mutex_lock failed: %d", err);
    }
  }

  PthreadMutexHolder(const PthreadMutexHolder &rhs) = delete;
  PthreadMutexHolder &operator=(const PthreadMutexHolder &rhs) = delete;

  ~PthreadMutexHolder() {
    const int err = pthread_mutex_unlock(mu_);
    if (err != 0) {
      ABSL_RAW_LOG(FATAL, "pthread_mutex_unlock failed: %d", err);
    }
  }

 private:
  pthread_mutex_t *mu_;
};

Waiter::Waiter() {
  const int err = pthread_mutex_init(&mu_, 0);
  if (err != 0) {
    ABSL_RAW_LOG(FATAL, "pthread_mutex_init failed: %d", err);
  }

  const int err2 = pthread_cond_init(&cv_, 0);
  if (err2 != 0) {
    ABSL_RAW_LOG(FATAL, "pthread_cond_init failed: %d", err2);
  }

  waiter_count_ = 0;
  wakeup_count_ = 0;
}

bool Waiter::Wait(KernelTimeout t) {
  struct timespec abs_timeout;
  if (t.has_timeout()) {
    abs_timeout = t.MakeAbsTimespec();
  }

  PthreadMutexHolder h(&mu_);
  ++waiter_count_;
  // Loop until we find a wakeup to consume or timeout.
  // Note that, since the thread ticker is just reset, we don't need to check
  // whether the thread is idle on the very first pass of the loop.
  bool first_pass = true;
  while (wakeup_count_ == 0) {
    if (!first_pass) MaybeBecomeIdle();
    // No wakeups available, time to wait.
    if (!t.has_timeout()) {
      const int err = pthread_cond_wait(&cv_, &mu_);
      if (err != 0) {
        ABSL_RAW_LOG(FATAL, "pthread_cond_wait failed: %d", err);
      }
    } else {
      const int err = pthread_cond_timedwait(&cv_, &mu_, &abs_timeout);
      if (err == ETIMEDOUT) {
        --waiter_count_;
        return false;
      }
      if (err != 0) {
        ABSL_RAW_LOG(FATAL, "pthread_cond_timedwait failed: %d", err);
      }
    }
    first_pass = false;
  }
  // Consume a wakeup and we're done.
  --wakeup_count_;
  --waiter_count_;
  return true;
}

void Waiter::Post() {
  PthreadMutexHolder h(&mu_);
  ++wakeup_count_;
  InternalCondVarPoke();
}

void Waiter::Poke() {
  PthreadMutexHolder h(&mu_);
  InternalCondVarPoke();
}

void Waiter::InternalCondVarPoke() {
  if (waiter_count_ != 0) {
    const int err = pthread_cond_signal(&cv_);
    if (ABSL_PREDICT_FALSE(err != 0)) {
      ABSL_RAW_LOG(FATAL, "pthread_cond_signal failed: %d", err);
    }
  }
}

#elif ABSL_WAITER_MODE == ABSL_WAITER_MODE_SEM

Waiter::Waiter() {
  if (sem_init(&sem_, 0, 0) != 0) {
    ABSL_RAW_LOG(FATAL, "sem_init failed with errno %d\n", errno);
  }
  wakeups_.store(0, std::memory_order_relaxed);
}

bool Waiter::Wait(KernelTimeout t) {
  struct timespec abs_timeout;
  if (t.has_timeout()) {
    abs_timeout = t.MakeAbsTimespec();
  }

  // Loop until we timeout or consume a wakeup.
  // Note that, since the thread ticker is just reset, we don't need to check
  // whether the thread is idle on the very first pass of the loop.
  bool first_pass = true;
  while (true) {
    int x = wakeups_.load(std::memory_order_relaxed);
    while (x != 0) {
      if (!wakeups_.compare_exchange_weak(x, x - 1,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed)) {
        continue;  // Raced with someone, retry.
      }
      // Successfully consumed a wakeup, we're done.
      return true;
    }

    if (!first_pass) MaybeBecomeIdle();
    // Nothing to consume, wait (looping on EINTR).
    while (true) {
      if (!t.has_timeout()) {
        if (sem_wait(&sem_) == 0) break;
        if (errno == EINTR) continue;
        ABSL_RAW_LOG(FATAL, "sem_wait failed: %d", errno);
      } else {
        if (sem_timedwait(&sem_, &abs_timeout) == 0) break;
        if (errno == EINTR) continue;
        if (errno == ETIMEDOUT) return false;
        ABSL_RAW_LOG(FATAL, "sem_timedwait failed: %d", errno);
      }
    }
    first_pass = false;
  }
}

void Waiter::Post() {
  // Post a wakeup.
  if (wakeups_.fetch_add(1, std::memory_order_release) == 0) {
    // We incremented from 0, need to wake a potential waiter.
    Poke();
  }
}

void Waiter::Poke() {
  if (sem_post(&sem_) != 0) {  // Wake any semaphore waiter.
    ABSL_RAW_LOG(FATAL, "sem_post failed with errno %d\n", errno);
  }
}

#elif ABSL_WAITER_MODE == ABSL_WAITER_MODE_WIN32

class Waiter::WinHelper {
 public:
  static SRWLOCK *GetLock(Waiter *w) {
    return reinterpret_cast<SRWLOCK *>(&w->mu_storage_);
  }

  static CONDITION_VARIABLE *GetCond(Waiter *w) {
    return reinterpret_cast<CONDITION_VARIABLE *>(&w->cv_storage_);
  }

  static_assert(sizeof(SRWLOCK) == sizeof(void *),
                "`mu_storage_` does not have the same size as SRWLOCK");
  static_assert(alignof(SRWLOCK) == alignof(void *),
                "`mu_storage_` does not have the same alignment as SRWLOCK");

  static_assert(sizeof(CONDITION_VARIABLE) == sizeof(void *),
                "`ABSL_CONDITION_VARIABLE_STORAGE` does not have the same size "
                "as `CONDITION_VARIABLE`");
  static_assert(
      alignof(CONDITION_VARIABLE) == alignof(void *),
      "`cv_storage_` does not have the same alignment as `CONDITION_VARIABLE`");

  // The SRWLOCK and CONDITION_VARIABLE types must be trivially constructible
  // and destructible because we never call their constructors or destructors.
  static_assert(std::is_trivially_constructible<SRWLOCK>::value,
                "The `SRWLOCK` type must be trivially constructible");
  static_assert(
      std::is_trivially_constructible<CONDITION_VARIABLE>::value,
      "The `CONDITION_VARIABLE` type must be trivially constructible");
  static_assert(std::is_trivially_destructible<SRWLOCK>::value,
                "The `SRWLOCK` type must be trivially destructible");
  static_assert(std::is_trivially_destructible<CONDITION_VARIABLE>::value,
                "The `CONDITION_VARIABLE` type must be trivially destructible");
};

class LockHolder {
 public:
  explicit LockHolder(SRWLOCK* mu) : mu_(mu) {
    AcquireSRWLockExclusive(mu_);
  }

  LockHolder(const LockHolder&) = delete;
  LockHolder& operator=(const LockHolder&) = delete;

  ~LockHolder() {
    ReleaseSRWLockExclusive(mu_);
  }

 private:
  SRWLOCK* mu_;
};

Waiter::Waiter() {
  auto *mu = ::new (static_cast<void *>(&mu_storage_)) SRWLOCK;
  auto *cv = ::new (static_cast<void *>(&cv_storage_)) CONDITION_VARIABLE;
  InitializeSRWLock(mu);
  InitializeConditionVariable(cv);
  waiter_count_ = 0;
  wakeup_count_ = 0;
}

bool Waiter::Wait(KernelTimeout t) {
  SRWLOCK *mu = WinHelper::GetLock(this);
  CONDITION_VARIABLE *cv = WinHelper::GetCond(this);

  LockHolder h(mu);
  ++waiter_count_;

  // Loop until we find a wakeup to consume or timeout.
  // Note that, since the thread ticker is just reset, we don't need to check
  // whether the thread is idle on the very first pass of the loop.
  bool first_pass = true;
  while (wakeup_count_ == 0) {
    if (!first_pass) MaybeBecomeIdle();
    // No wakeups available, time to wait.
    if (!SleepConditionVariableSRW(cv, mu, t.InMillisecondsFromNow(), 0)) {
      // GetLastError() returns a Win32 DWORD, but we assign to
      // unsigned long to simplify the ABSL_RAW_LOG case below.  The uniform
      // initialization guarantees this is not a narrowing conversion.
      const unsigned long err{GetLastError()};  // NOLINT(runtime/int)
      if (err == ERROR_TIMEOUT) {
        --waiter_count_;
        return false;
      } else {
        ABSL_RAW_LOG(FATAL, "SleepConditionVariableSRW failed: %lu", err);
      }
    }
    first_pass = false;
  }
  // Consume a wakeup and we're done.
  --wakeup_count_;
  --waiter_count_;
  return true;
}

void Waiter::Post() {
  LockHolder h(WinHelper::GetLock(this));
  ++wakeup_count_;
  InternalCondVarPoke();
}

void Waiter::Poke() {
  LockHolder h(WinHelper::GetLock(this));
  InternalCondVarPoke();
}

void Waiter::InternalCondVarPoke() {
  if (waiter_count_ != 0) {
    WakeConditionVariable(WinHelper::GetCond(this));
  }
}

#else
#error Unknown ABSL_WAITER_MODE
#endif

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl
