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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SYNC

#include <errno.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <time.h>
#include "src/core/lib/profiling/timers.h"

#ifdef GPR_LOW_LEVEL_COUNTERS
gpr_atm gpr_mu_locks = 0;
gpr_atm gpr_counter_atm_cas = 0;
gpr_atm gpr_counter_atm_add = 0;
#endif

void gpr_mu_init(gpr_mu* mu) { GPR_ASSERT(pthread_mutex_init(mu, NULL) == 0); }

void gpr_mu_destroy(gpr_mu* mu) { GPR_ASSERT(pthread_mutex_destroy(mu) == 0); }

void gpr_mu_lock(gpr_mu* mu) {
#ifdef GPR_LOW_LEVEL_COUNTERS
  GPR_ATM_INC_COUNTER(gpr_mu_locks);
#endif
  GPR_TIMER_BEGIN("gpr_mu_lock", 0);
  GPR_ASSERT(pthread_mutex_lock(mu) == 0);
  GPR_TIMER_END("gpr_mu_lock", 0);
}

void gpr_mu_unlock(gpr_mu* mu) {
  GPR_TIMER_BEGIN("gpr_mu_unlock", 0);
  GPR_ASSERT(pthread_mutex_unlock(mu) == 0);
  GPR_TIMER_END("gpr_mu_unlock", 0);
}

int gpr_mu_trylock(gpr_mu* mu) {
  int err;
  GPR_TIMER_BEGIN("gpr_mu_trylock", 0);
  err = pthread_mutex_trylock(mu);
  GPR_ASSERT(err == 0 || err == EBUSY);
  GPR_TIMER_END("gpr_mu_trylock", 0);
  return err == 0;
}

/*----------------------------------------*/

void gpr_cv_init(gpr_cv* cv) { GPR_ASSERT(pthread_cond_init(cv, NULL) == 0); }

void gpr_cv_destroy(gpr_cv* cv) { GPR_ASSERT(pthread_cond_destroy(cv) == 0); }

int gpr_cv_wait(gpr_cv* cv, gpr_mu* mu, gpr_timespec abs_deadline) {
  int err = 0;
  if (gpr_time_cmp(abs_deadline, gpr_inf_future(abs_deadline.clock_type)) ==
      0) {
    err = pthread_cond_wait(cv, mu);
  } else {
    struct timespec abs_deadline_ts;
    abs_deadline = gpr_convert_clock_type(abs_deadline, GPR_CLOCK_REALTIME);
    abs_deadline_ts.tv_sec = (time_t)abs_deadline.tv_sec;
    abs_deadline_ts.tv_nsec = abs_deadline.tv_nsec;
    err = pthread_cond_timedwait(cv, mu, &abs_deadline_ts);
  }
  GPR_ASSERT(err == 0 || err == ETIMEDOUT || err == EAGAIN);
  return err == ETIMEDOUT;
}

void gpr_cv_signal(gpr_cv* cv) { GPR_ASSERT(pthread_cond_signal(cv) == 0); }

void gpr_cv_broadcast(gpr_cv* cv) {
  GPR_ASSERT(pthread_cond_broadcast(cv) == 0);
}

/*----------------------------------------*/

void gpr_once_init(gpr_once* once, void (*init_function)(void)) {
  GPR_ASSERT(pthread_once(once, init_function) == 0);
}

#endif /* GRP_POSIX_SYNC */
