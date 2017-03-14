/*
 *
 * Copyright 2017, Google Inc.
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

// \file Arena based allocator
// Allows very fast allocation of memory, but that memory cannot be freed until
// the arena as a whole is freed
// Tracks the total memory allocated against it, so that future arenas can
// pre-allocate the right amount of memory

#ifndef GRPC_CORE_LIB_SUPPORT_ARENA_H
#define GRPC_CORE_LIB_SUPPORT_ARENA_H

#include <stddef.h>

typedef struct gpr_arena gpr_arena;

// Create an arena, with \a initial_size bytes in the first allocated buffer
gpr_arena *gpr_arena_create(size_t initial_size);
// Allocate \a size bytes from the arena
void *gpr_arena_alloc(gpr_arena *arena, size_t size);
// Destroy an arena, returning the total number of bytes allocated
size_t gpr_arena_destroy(gpr_arena *arena);

#endif /* GRPC_CORE_LIB_SUPPORT_ARENA_H */
