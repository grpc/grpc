/*
 *
 * Copyright 2014, Google Inc.
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

#ifdef GPR_CPU_LINUX

#include "src/core/support/cpu.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#define GRPC_GNU_SOURCE
#endif

#ifndef __USE_GNU
#define __USE_GNU
#define GRPC_USE_GNU
#endif

#ifndef __USE_MISC
#define __USE_MISC
#define GRPC_USE_MISC
#endif

#include <sched.h>

#ifdef GRPC_GNU_SOURCE
#undef _GNU_SOURCE
#undef GRPC_GNU_SOURCE
#endif

#ifdef GRPC_USE_GNU
#undef __USE_GNU
#undef GRPC_USE_GNU
#endif

#ifdef GRPC_USE_MISC
#undef __USE_MISC
#undef GRPC_USE_MISC
#endif

#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <grpc/support/log.h>

unsigned gpr_cpu_num_cores(void) {
  static int ncpus = 0;
  /* FIXME: !threadsafe */
  if (ncpus == 0) {
    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus < 1) {
      gpr_log(GPR_ERROR, "Cannot determine number of CPUs: assuming 1");
      ncpus = 1;
    }
  }
  return ncpus;
}

unsigned gpr_cpu_current_cpu(void) {
  int cpu = sched_getcpu();
  if (cpu < 0) {
    gpr_log(GPR_ERROR, "Error determining current CPU: %s\n", strerror(errno));
    return 0;
  }
  return cpu;
}

#endif /* GPR_CPU_LINUX */
