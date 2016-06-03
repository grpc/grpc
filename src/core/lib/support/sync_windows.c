/*
 *
 * Copyright 2015, Google Inc.
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

/* Win32 code for gpr synchronization support. */

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

void gpr_mu_init(gpr_mu *mu) {
  InitializeCriticalSection(&mu->cs);
  mu->locked = 0;
}

void gpr_mu_destroy(gpr_mu *mu) { DeleteCriticalSection(&mu->cs); }

void gpr_mu_lock(gpr_mu *mu) {
  EnterCriticalSection(&mu->cs);
  GPR_ASSERT(!mu->locked);
  mu->locked = 1;
}

void gpr_mu_unlock(gpr_mu *mu) {
  mu->locked = 0;
  LeaveCriticalSection(&mu->cs);
}

int gpr_mu_trylock(gpr_mu *mu) {
  int result = TryEnterCriticalSection(&mu->cs);
  if (result) {
    if (mu->locked) {                /* This thread already holds the lock. */
      LeaveCriticalSection(&mu->cs); /* Decrement lock count. */
      result = 0;                    /* Indicate failure */
    }
    mu->locked = 1;
  }
  return result;
}

/*----------------------------------------*/

void gpr_cv_init(gpr_cv *cv) { InitializeConditionVariable(cv); }

void gpr_cv_destroy(gpr_cv *cv) {
  /* Condition variables don't need destruction in Win32. */
}

int gpr_cv_wait(gpr_cv *cv, gpr_mu *mu, gpr_timespec abs_deadline) {
  int timeout = 0;
  DWORD timeout_max_ms;
  mu->locked = 0;
  if (gpr_time_cmp(abs_deadline, gpr_inf_future(abs_deadline.clock_type)) ==
      0) {
    SleepConditionVariableCS(cv, &mu->cs, INFINITE);
  } else {
    abs_deadline = gpr_convert_clock_type(abs_deadline, GPR_CLOCK_REALTIME);
    gpr_timespec now = gpr_now(abs_deadline.clock_type);
    int64_t now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    int64_t deadline_ms =
        (int64_t)abs_deadline.tv_sec * 1000 + abs_deadline.tv_nsec / 1000000;
    if (now_ms >= deadline_ms) {
      timeout = 1;
    } else {
      if ((deadline_ms - now_ms) >= INFINITE) {
        timeout_max_ms = INFINITE - 1;
      } else {
        timeout_max_ms = (DWORD)(deadline_ms - now_ms);
      }
      timeout = (SleepConditionVariableCS(cv, &mu->cs, timeout_max_ms) == 0 &&
                 GetLastError() == ERROR_TIMEOUT);
    }
  }
  mu->locked = 1;
  return timeout;
}

void gpr_cv_signal(gpr_cv *cv) { WakeConditionVariable(cv); }

void gpr_cv_broadcast(gpr_cv *cv) { WakeAllConditionVariable(cv); }

/*----------------------------------------*/

static void *dummy;
struct run_once_func_arg {
  void (*init_function)(void);
};
static BOOL CALLBACK run_once_func(gpr_once *once, void *v, void **pv) {
  struct run_once_func_arg *arg = v;
  (*arg->init_function)();
  return 1;
}

void gpr_once_init(gpr_once *once, void (*init_function)(void)) {
  struct run_once_func_arg arg;
  arg.init_function = init_function;
  InitOnceExecuteOnce(once, run_once_func, &arg, &dummy);
}

#endif /* GPR_WINDOWS */
