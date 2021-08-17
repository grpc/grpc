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

#include <grpc/support/cpu.h>

#ifdef GPR_CPU_IPHONE

#include <sys/sysctl.h>

unsigned gpr_cpu_num_cores(void) {
  size_t len;
  unsigned int ncpu;
  len = sizeof(ncpu);
  sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0);

  return ncpu;
}

/* Most code that's using this is using it to shard across work queues. So
   unless profiling shows it's a problem or there appears a way to detect the
   currently running CPU core, let's have it shard the default way.
   Note that the interface in cpu.h lets gpr_cpu_num_cores return 0, but doing
   it makes it impossible for gpr_cpu_current_cpu to satisfy its stated range,
   and some code might be relying on it. */
unsigned gpr_cpu_current_cpu(void) { return 0; }

#endif /* GPR_CPU_IPHONE */
