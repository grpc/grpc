/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
