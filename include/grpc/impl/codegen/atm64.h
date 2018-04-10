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

#ifndef GRPC_IMPL_CODEGEN_ATM64_H
#define GRPC_IMPL_CODEGEN_ATM64_H

/** This interface provides atomic operations and barriers for 64 bit integer
   data types (instead of intptr_t so that this works on both 32-bit and 64-bit
   systems.

   It is internal to gpr support code and should not be used outside it.

   If an operation with acquire semantics precedes another memory access by the
   same thread, the operation will precede that other access as seen by other
   threads.

   If an operation with release semantics follows another memory access by the
   same thread, the operation will follow that other access as seen by other
   threads.

   Routines with "acq" or "full" in the name have acquire semantics.  Routines
   with "rel" or "full" in the name have release semantics.  Routines with
   "no_barrier" in the name have neither acquire not release semantics.

   The routines may be implemented as macros.

   // Atomic operations act on an intergral_type gpr_atm64 that is 64 bit wide
   typedef int64_t gpr_atm64;

   gpr_atm64 gpr_atm64_no_barrier_load(gpr_atm64 *p);

   // Atomically set *p = value, with release semantics.
   void gpr_atm64_no_barrier_store(gpr_atm64 *p, gpr_atm64 value);
*/

#include <grpc/impl/codegen/port_platform.h>

#if defined(GPR_GCC_ATOMIC)
#include <grpc/impl/codegen/atm64_gcc_atomic.h>
#elif defined(GPR_GCC_SYNC)
#include <grpc/impl/codegen/atm64_gcc_sync.h>
#elif defined(GPR_WINDOWS_ATOMIC)
#include <grpc/impl/codegen/atm64_windows.h>
#else
#error could not determine platform for atm
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_ATM64_H */
