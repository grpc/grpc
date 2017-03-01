/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_SUPPORT_ALLOC_H
#define GRPC_SUPPORT_ALLOC_H

#include <stddef.h>

#include <grpc/impl/codegen/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpr_allocation_functions {
  void *(*malloc_fn)(size_t size);
  void *(*zalloc_fn)(size_t size); /* if NULL, uses malloc_fn then memset */
  void *(*realloc_fn)(void *ptr, size_t size);
  void (*free_fn)(void *ptr);
} gpr_allocation_functions;

/* malloc.
 * If size==0, always returns NULL. Otherwise this function never returns NULL.
 * The pointer returned is suitably aligned for any kind of variable it could
 * contain.
 */
GPRAPI void *gpr_malloc(size_t size);
/* like malloc, but zero all bytes before returning them */
GPRAPI void *gpr_zalloc(size_t size);
/* free */
GPRAPI void gpr_free(void *ptr);
/* realloc, never returns NULL */
GPRAPI void *gpr_realloc(void *p, size_t size);
/* aligned malloc, never returns NULL, will align to 1 << alignment_log */
GPRAPI void *gpr_malloc_aligned(size_t size, size_t alignment_log);
/* free memory allocated by gpr_malloc_aligned */
GPRAPI void gpr_free_aligned(void *ptr);

/** Request the family of allocation functions in \a functions be used. NOTE
 * that this request will be honored in a *best effort* basis and that no
 * guarantees are made about the default functions (eg, malloc) being called. */
GPRAPI void gpr_set_allocation_functions(gpr_allocation_functions functions);

/** Return the family of allocation functions currently in effect. */
GPRAPI gpr_allocation_functions gpr_get_allocation_functions();

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_ALLOC_H */
