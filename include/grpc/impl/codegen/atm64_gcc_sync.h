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

#ifndef GRPC_IMPL_CODEGEN_ATM64_GCC_SYNC64_H
#define GRPC_IMPL_CODEGEN_ATM64_GCC_SYNC64_H

/* variant of atm_platform.h for gcc and gcc-like compiers with __sync_*
   interface */
#include <grpc/impl/codegen/port_platform.h>

typedef int64_t gpr_atm64;
#define GPR_ATM64_MAX INT64_MAX
#define GPR_ATM64_MIN INT64_MIN

#define GPR_ATM64_COMPILE_BARRIER_() __asm__ __volatile__("" : : : "memory")

static __inline gpr_atm64 gpr_atm64_no_barrier_load(const gpr_atm64* p) {
  gpr_atm64 value = *p;
  GPR_ATM64_COMPILE_BARRIER_();
  return value;
}

static __inline void gpr_atm64_no_barrier_store(gpr_atm64* p, gpr_atm64 value) {
  GPR_ATM64_COMPILE_BARRIER_();
  *p = value;
}

#endif /* GRPC_IMPL_CODEGEN_ATM64_GCC_SYNC64_H */
