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

#ifndef GRPC_SUPPORT_ALLOC_H
#define GRPC_SUPPORT_ALLOC_H

#include <stddef.h>

#include <grpc/impl/codegen/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpr_allocation_functions {
  void* (*malloc_fn)(size_t size);
  void* (*zalloc_fn)(size_t size); /** if NULL, uses malloc_fn then memset */
  void* (*realloc_fn)(void* ptr, size_t size);
  void (*free_fn)(void* ptr);
} gpr_allocation_functions;

/** malloc.
 * If size==0, always returns NULL. Otherwise this function never returns NULL.
 * The pointer returned is suitably aligned for any kind of variable it could
 * contain.
 */
GPRAPI void* gpr_malloc(size_t size);
/** like malloc, but zero all bytes before returning them */
GPRAPI void* gpr_zalloc(size_t size);
/** free */
GPRAPI void gpr_free(void* ptr);
/** realloc, never returns NULL */
GPRAPI void* gpr_realloc(void* p, size_t size);
/** aligned malloc, never returns NULL, will align to 1 << alignment_log */
GPRAPI void* gpr_malloc_aligned(size_t size, size_t alignment_log);
/** free memory allocated by gpr_malloc_aligned */
GPRAPI void gpr_free_aligned(void* ptr);

/** Request the family of allocation functions in \a functions be used. NOTE
 * that this request will be honored in a *best effort* basis and that no
 * guarantees are made about the default functions (eg, malloc) being called.
 * The functions.free_fn implementation must be a no-op for NULL input. */
GPRAPI void gpr_set_allocation_functions(gpr_allocation_functions functions);

/** Return the family of allocation functions currently in effect. */
GPRAPI gpr_allocation_functions gpr_get_allocation_functions();

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_ALLOC_H */
