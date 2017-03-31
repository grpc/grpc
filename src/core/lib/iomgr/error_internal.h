/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_CORE_LIB_IOMGR_ERROR_INTERNAL_H
#define GRPC_CORE_LIB_IOMGR_ERROR_INTERNAL_H

#include <inttypes.h>
#include <stdbool.h>  // TODO, do we need this?

#include <grpc/support/sync.h>

typedef struct grpc_linked_error grpc_linked_error;

struct grpc_linked_error {
  grpc_error *err;
  uint8_t next;
};

// c core representation of an error. See error.h for high level description of
// this object.
struct grpc_error {
  // All atomics in grpc_error must be stored in this nested struct. The rest of
  // the object is memcpy-ed in bulk in copy_and_unref.
  struct atomics {
    gpr_refcount refs;
    gpr_atm error_string;
  } atomics;
  // These arrays index into dynamic arena at the bottom of the struct.
  // UINT8_MAX is used as a sentinel value.
  uint8_t ints[GRPC_ERROR_INT_MAX];
  uint8_t strs[GRPC_ERROR_STR_MAX];
  uint8_t times[GRPC_ERROR_TIME_MAX];
  // The child errors are stored in the arena, but are effectively a linked list
  // structure, since they are contained withing grpc_linked_error objects.
  uint8_t first_err;
  uint8_t last_err;
  // The arena is dynamically reallocated with a grow factor of 1.5.
  uint8_t arena_size;
  uint8_t arena_capacity;
  intptr_t arena[0];
};

bool grpc_error_is_special(grpc_error *err);

#endif /* GRPC_CORE_LIB_IOMGR_ERROR_INTERNAL_H */
