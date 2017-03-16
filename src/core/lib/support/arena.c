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

#include "src/core/lib/support/arena.h"
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#define ROUND_UP_TO_ALIGNMENT_SIZE(x) \
  (((x) + GPR_MAX_ALIGNMENT - 1u) & ~(GPR_MAX_ALIGNMENT - 1u))

typedef struct zone {
  size_t size_begin;
  size_t size_end;
  gpr_atm next_atm;
} zone;

struct gpr_arena {
  gpr_atm size_so_far;
  zone initial_zone;
};

gpr_arena *gpr_arena_create(size_t initial_size) {
  initial_size = ROUND_UP_TO_ALIGNMENT_SIZE(initial_size);
  gpr_arena *a = gpr_zalloc(sizeof(gpr_arena) + initial_size);
  a->initial_zone.size_end = initial_size;
  return a;
}

size_t gpr_arena_destroy(gpr_arena *arena) {
  gpr_atm size = gpr_atm_no_barrier_load(&arena->size_so_far);
  zone *z = (zone *)gpr_atm_no_barrier_load(&arena->initial_zone.next_atm);
  gpr_free(arena);
  while (z) {
    zone *next_z = (zone *)gpr_atm_no_barrier_load(&z->next_atm);
    gpr_free(z);
    z = next_z;
  }
  return (size_t)size;
}

void *gpr_arena_alloc(gpr_arena *arena, size_t size) {
  size = ROUND_UP_TO_ALIGNMENT_SIZE(size);
  size_t start =
      (size_t)gpr_atm_no_barrier_fetch_add(&arena->size_so_far, size);
  zone *z = &arena->initial_zone;
  while (start > z->size_end) {
    zone *next_z = (zone *)gpr_atm_acq_load(&z->next_atm);
    if (next_z == NULL) {
      size_t next_z_size = (size_t)gpr_atm_no_barrier_load(&arena->size_so_far);
      next_z = gpr_zalloc(sizeof(zone) + next_z_size);
      next_z->size_begin = z->size_end;
      next_z->size_end = z->size_end + next_z_size;
      if (!gpr_atm_rel_cas(&z->next_atm, (gpr_atm)NULL, (gpr_atm)next_z)) {
        gpr_free(next_z);
        next_z = (zone *)gpr_atm_acq_load(&z->next_atm);
      }
    }
    z = next_z;
  }
  if (start + size > z->size_end) {
    return gpr_arena_alloc(arena, size);
  }
  GPR_ASSERT(start >= z->size_begin);
  GPR_ASSERT(start + size <= z->size_end);
  return ((char *)(z + 1)) + start - z->size_begin;
}
