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
#include <grpc/support/time.h>

Timer::Timer() : start_(Sample()) {}

double Timer::Now() {
  auto ts = gpr_now(GPR_CLOCK_REALTIME);
  return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

static double time_double(struct timeval* tv) {
  return tv->tv_sec + 1e-6 * tv->tv_usec;
}

Timer::Result Timer::Sample() {
  struct rusage usage;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  getrusage(RUSAGE_SELF, &usage);

  Result r;
  r.wall = time_double(&tv);
  r.user = time_double(&usage.ru_utime);
  r.system = time_double(&usage.ru_stime);
  return r;
}

Timer::Result Timer::Mark() const {
  Result s = Sample();
  Result r;
  r.wall = s.wall - start_.wall;
  r.user = s.user - start_.user;
  r.system = s.system - start_.system;
  return r;
}
