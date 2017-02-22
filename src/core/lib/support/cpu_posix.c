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

#ifdef GPR_CPU_POSIX

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

static __thread char magic_thread_local;

static long ncpus = 0;

static void init_ncpus() {
  ncpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (ncpus < 1 || ncpus > INT32_MAX) {
    gpr_log(GPR_ERROR, "Cannot determine number of CPUs: assuming 1");
    ncpus = 1;
  }
}

unsigned gpr_cpu_num_cores(void) {
  static gpr_once once = GPR_ONCE_INIT;
  gpr_once_init(&once, init_ncpus);
  return (unsigned)ncpus;
}

unsigned gpr_cpu_current_cpu(void) {
  /* NOTE: there's no way I know to return the actual cpu index portably...
     most code that's using this is using it to shard across work queues though,
     so here we use thread identity instead to achieve a similar though not
     identical effect */
  return (unsigned)GPR_HASH_POINTER(&magic_thread_local, gpr_cpu_num_cores());
}

#endif /* GPR_CPU_POSIX */
