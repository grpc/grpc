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

#include "test/cpp/qps/timer.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpc++/config.h>

Timer::Timer() : start_(Sample()) {}

double Timer::Now() {
  auto ts = gpr_now();
  return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

static double time_double(struct timeval* tv) {
  return tv->tv_sec + 1e-6 * tv->tv_usec;
}

Timer::Result Timer::Sample() {
  struct rusage usage;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  getrusage(RUSAGE_SELF, &usage);

  Result r;
  r.wall = time_double(&tv);
  r.user = time_double(&usage.ru_utime);
  r.system = time_double(&usage.ru_stime);
#ifdef GPR_PERF_COUNTERS
  r.malloc_calls = gpr_stats_read(&gpr_alloc_calls);
  r.mutex_locks = gpr_stats_read(&gpr_mutex_locks);
  r.cv_waits = gpr_stats_read(&gpr_cv_waits);
#else
  r.malloc_calls = r.mutex_locks = r.cv_waits = 0;
#endif
  return r;
}

Timer::Result Timer::Mark() {
  Result s = Sample();
  Result r;
  r.wall = s.wall - start_.wall;
  r.user = s.user - start_.user;
  r.system = s.system - start_.system;
  r.malloc_calls = s.malloc_calls - start_.malloc_calls;
  r.mutex_locks = s.mutex_locks - start_.mutex_locks;
  r.cv_waits = s.cv_waits - start_.cv_waits;
  start_ = s;
  return r;
}
