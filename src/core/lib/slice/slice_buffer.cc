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

#include <grpc/support/port_platform.h>

#include <grpc/slice_buffer.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_buffer_internal.h"
#include "src/core/lib/slice/slice_internal.h"

void grpc_slice_buffer_init(grpc_slice_buffer* sb) {
  grpc_slice_buffer_init_internal(sb);
}

void grpc_slice_buffer_destroy(grpc_slice_buffer* sb) {
  if (grpc_core::ExecCtx::Get() == nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_slice_buffer_destroy_internal(sb);
  } else {
    grpc_slice_buffer_destroy_internal(sb);
  }
}

uint8_t* grpc_slice_buffer_tiny_add(grpc_slice_buffer* sb, size_t n) {
  return grpc_slice_buffer_tiny_add_internal(sb, n);
}

size_t grpc_slice_buffer_add_indexed(grpc_slice_buffer* sb, grpc_slice s) {
  return grpc_slice_buffer_add_indexed_internal(sb, s);
}

void grpc_slice_buffer_add(grpc_slice_buffer* sb, grpc_slice s) {
  grpc_slice_buffer_add_internal(sb, s);
}

void grpc_slice_buffer_addn(grpc_slice_buffer* sb, grpc_slice* s, size_t n) {
  grpc_slice_buffer_addn_internal(sb, s, n);
}

void grpc_slice_buffer_pop(grpc_slice_buffer* sb) {
  grpc_slice_buffer_pop_internal(sb);
}

void grpc_slice_buffer_reset_and_unref(grpc_slice_buffer* sb) {
  if (grpc_core::ExecCtx::Get() == nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_slice_buffer_reset_and_unref_internal(sb);
  } else {
    grpc_slice_buffer_reset_and_unref_internal(sb);
  }
}

void grpc_slice_buffer_swap(grpc_slice_buffer* a, grpc_slice_buffer* b) {
  grpc_slice_buffer_swap_internal(a, b);
}

void grpc_slice_buffer_move_into(grpc_slice_buffer* src,
                                 grpc_slice_buffer* dst) {
  grpc_slice_buffer_move_into_internal(src, dst);
}

static void slice_buffer_move_first_maybe_ref(grpc_slice_buffer* src, size_t n,
                                              grpc_slice_buffer* dst,
                                              bool incref) {
  GPR_ASSERT(src->length >= n);
  if (src->length == n) {
    grpc_slice_buffer_move_into(src, dst);
    return;
  }

  size_t output_len = dst->length + n;
  size_t new_input_len = src->length - n;

  while (src->count > 0) {
    grpc_slice slice = grpc_slice_buffer_take_first(src);
    size_t slice_len = GRPC_SLICE_LENGTH(slice);
    if (n > slice_len) {
      grpc_slice_buffer_add(dst, slice);
      n -= slice_len;
    } else if (n == slice_len) {
      grpc_slice_buffer_add(dst, slice);
      break;
    } else if (incref) { /* n < slice_len */
      grpc_slice_buffer_undo_take_first(
          src, grpc_slice_split_tail_maybe_ref(&slice, n, GRPC_SLICE_REF_BOTH));
      GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == n);
      grpc_slice_buffer_add(dst, slice);
      break;
    } else { /* n < slice_len */
      grpc_slice_buffer_undo_take_first(
          src, grpc_slice_split_tail_maybe_ref(&slice, n, GRPC_SLICE_REF_TAIL));
      GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == n);
      grpc_slice_buffer_add_indexed(dst, slice);
      break;
    }
  }
  GPR_ASSERT(dst->length == output_len);
  GPR_ASSERT(src->length == new_input_len);
  GPR_ASSERT(src->count > 0);
}

void grpc_slice_buffer_move_first(grpc_slice_buffer* src, size_t n,
                                  grpc_slice_buffer* dst) {
  slice_buffer_move_first_maybe_ref(src, n, dst, true);
}

void grpc_slice_buffer_move_first_no_ref(grpc_slice_buffer* src, size_t n,
                                         grpc_slice_buffer* dst) {
  slice_buffer_move_first_maybe_ref(src, n, dst, false);
}

void grpc_slice_buffer_move_first_into_buffer(grpc_slice_buffer* src, size_t n,
                                              void* dst) {
  char* dstp = static_cast<char*>(dst);
  GPR_ASSERT(src->length >= n);

  while (n > 0) {
    grpc_slice slice = grpc_slice_buffer_take_first(src);
    size_t slice_len = GRPC_SLICE_LENGTH(slice);
    if (slice_len > n) {
      memcpy(dstp, GRPC_SLICE_START_PTR(slice), n);
      grpc_slice_buffer_undo_take_first(
          src, grpc_slice_sub_no_ref(slice, n, slice_len));
      n = 0;
    } else if (slice_len == n) {
      memcpy(dstp, GRPC_SLICE_START_PTR(slice), n);
      grpc_slice_unref_internal(slice);
      n = 0;
    } else {
      memcpy(dstp, GRPC_SLICE_START_PTR(slice), slice_len);
      dstp += slice_len;
      n -= slice_len;
      grpc_slice_unref_internal(slice);
    }
  }
}

void grpc_slice_buffer_trim_end(grpc_slice_buffer* sb, size_t n,
                                grpc_slice_buffer* garbage) {
  GPR_ASSERT(n <= sb->length);
  sb->length -= n;
  for (;;) {
    size_t idx = sb->count - 1;
    grpc_slice slice = sb->slices[idx];
    size_t slice_len = GRPC_SLICE_LENGTH(slice);
    if (slice_len > n) {
      sb->slices[idx] = grpc_slice_split_head(&slice, slice_len - n);
      grpc_slice_buffer_add_indexed(garbage, slice);
      return;
    } else if (slice_len == n) {
      grpc_slice_buffer_add_indexed(garbage, slice);
      sb->count = idx;
      return;
    } else {
      grpc_slice_buffer_add_indexed(garbage, slice);
      n -= slice_len;
      sb->count = idx;
    }
  }
}

grpc_slice grpc_slice_buffer_take_first(grpc_slice_buffer* sb) {
  grpc_slice slice;
  GPR_ASSERT(sb->count > 0);
  slice = sb->slices[0];
  sb->slices++;
  sb->count--;
  sb->length -= GRPC_SLICE_LENGTH(slice);

  return slice;
}

void grpc_slice_buffer_undo_take_first(grpc_slice_buffer* sb,
                                       grpc_slice slice) {
  grpc_slice_buffer_undo_take_first_internal(sb, slice);
}
