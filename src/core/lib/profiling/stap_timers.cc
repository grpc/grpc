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

#ifdef GRPC_STAP_PROFILER

#include "src/core/lib/profiling/timers.h"

#include <sys/sdt.h>
/* Generated from src/core/profiling/stap_probes.d */
#include "src/core/lib/profiling/stap_probes.h"

/* Latency profiler API implementation. */
void gpr_timer_add_mark(int tag, const char* tagstr, void* id, const char* file,
                        int line) {
  _STAP_ADD_MARK(tag);
}

void gpr_timer_add_important_mark(int tag, const char* tagstr, void* id,
                                  const char* file, int line) {
  _STAP_ADD_IMPORTANT_MARK(tag);
}

void gpr_timer_begin(int tag, const char* tagstr, void* id, const char* file,
                     int line) {
  _STAP_TIMING_NS_BEGIN(tag);
}

void gpr_timer_end(int tag, const char* tagstr, void* id, const char* file,
                   int line) {
  _STAP_TIMING_NS_END(tag);
}

#endif /* GRPC_STAP_PROFILER */
