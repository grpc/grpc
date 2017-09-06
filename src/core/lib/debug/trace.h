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

#ifndef GRPC_CORE_LIB_DEBUG_TRACE_H
#define GRPC_CORE_LIB_DEBUG_TRACE_H

#include <grpc/support/atm.h>
#include <grpc/support/port_platform.h>
#include <stdbool.h>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define GRPC_THREADSAFE_TRACER
#endif
#endif

typedef struct {
#ifdef GRPC_THREADSAFE_TRACER
  gpr_atm value;
#else
  bool value;
#endif
  char *name;
} grpc_tracer_flag;

#ifdef GRPC_THREADSAFE_TRACER
#define GRPC_TRACER_ON(flag) (gpr_atm_no_barrier_load(&(flag).value) != 0)
#define GRPC_TRACER_INITIALIZER(on, name) \
  { (gpr_atm)(on), (name) }
#else
#define GRPC_TRACER_ON(flag) ((flag).value)
#define GRPC_TRACER_INITIALIZER(on, name) \
  { (on), (name) }
#endif

void grpc_register_tracer(grpc_tracer_flag *flag);
void grpc_tracer_init(const char *env_var_name);
void grpc_tracer_shutdown(void);

#endif /* GRPC_CORE_LIB_DEBUG_TRACE_H */
