/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_IMPL_CODEGEN_ATM64_GCC_ATOMIC_H
#define GRPC_IMPL_CODEGEN_ATM64_GCC_ATOMIC_H

/* atm_platform.h for gcc and gcc-like compilers with the
   __atomic_* interface.  */
#include <grpc/impl/codegen/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t gpr_atm64;
#define GPR_ATM64_MAX INT64_MAX
#define GPR_ATM64_MIN INT64_MIN

#define gpr_atm_no_barrier_load(p) (__atomic_load_n((p), __ATOMIC_RELAXED))
#define gpr_atm_no_barrier_store(p, value) \
  (__atomic_store_n((p), (int64_t)(value), __ATOMIC_RELAXED))

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_ATM64_GCC_ATOMIC_H */
