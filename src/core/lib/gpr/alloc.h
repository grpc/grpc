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

#ifndef GRPC_CORE_LIB_GPR_ALLOC_H
#define GRPC_CORE_LIB_GPR_ALLOC_H

#include <grpc/support/port_platform.h>

/// Given a size, round up to the next multiple of sizeof(void*).
#define GPR_ROUND_UP_TO_ALIGNMENT_SIZE(x, align) \
  (((x) + (align)-1u) & ~((align)-1u))

#define GPR_ROUND_UP_TO_MAX_ALIGNMENT_SIZE(x) \
  GPR_ROUND_UP_TO_ALIGNMENT_SIZE((x), GPR_MAX_ALIGNMENT)

#define GPR_ROUND_UP_TO_CACHELINE_SIZE(x) \
  GPR_ROUND_UP_TO_ALIGNMENT_SIZE((x), GPR_CACHELINE_SIZE)

void* gpr_malloc_aligned_fallback(size_t size, size_t alignment);
void gpr_free_aligned_fallback(void* ptr);

void* gpr_malloc_aligned_platform(size_t size, size_t alignment);
void gpr_free_aligned_platform(void* ptr);

#ifndef NDEBUG
static inline constexpr bool is_power_of_two(size_t value) {
  // 2^N =     100000...000
  // 2^N - 1 = 011111...111
  // (2^N) && ((2^N)-1)) = 0
  return (value & (value - 1)) == 0;
}
#endif

#endif /* GRPC_CORE_LIB_GPR_ALLOC_H */
