/*
 *
 * Copyright 2017 gRPC authors.
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

// \file Arena based allocator
// Allows very fast allocation of memory, but that memory cannot be freed until
// the arena as a whole is freed
// Tracks the total memory allocated against it, so that future arenas can
// pre-allocate the right amount of memory
// This transitional API is deprecated and will be removed soon in favour of
// src/core/lib/gprpp/arena.h .

#ifndef GRPC_CORE_LIB_GPRPP_ARENA_H
#define GRPC_CORE_LIB_GPRPP_ARENA_H

namespace grpc_core {
class Arena;
}  // namespace grpc_core

typedef class grpc_core::Arena gpr_arena;

// TODO(arjunroy) : Remove deprecated gpr_arena API once all callers are gone.
gpr_arena* gpr_arena_create(size_t initial_size);
// Destroy an arena, returning the total number of bytes allocated
size_t gpr_arena_destroy(gpr_arena* arena);
// Allocate \a size bytes from the arena
void* gpr_arena_alloc(gpr_arena* arena, size_t size);

#endif /* GRPC_CORE_LIB_GPRPP_ARENA_H */
