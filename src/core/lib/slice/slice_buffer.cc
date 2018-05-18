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
#include "src/core/lib/slice/slice_internal.h"

/* grow a buffer; requires GRPC_SLICE_BUFFER_INLINE_ELEMENTS > 1 */
#define GROW(x) (3 * (x) / 2)

static void maybe_embiggen(grpc_slice_buffer* sb) {
  /* How far away from sb->base_slices is sb->slices pointer */
  size_t slice_offset = static_cast<size_t>(sb->slices - sb->base_slices);
  size_t slice_count = sb->count + slice_offset;

  if (slice_count == sb->capacity) {
    if (sb->base_slices != sb->slices) {
      /* Make room by moving elements if there's still space unused */
      memmove(sb->base_slices, sb->slices, sb->count * sizeof(grpc_slice));
      sb->slices = sb->base_slices;
    } else {
      /* Allocate more memory if no more space is available */
      sb->capacity = GROW(sb->capacity);
      GPR_ASSERT(sb->capacity > slice_count);
      if (sb->base_slices == sb->inlined) {
        sb->base_slices = static_cast<grpc_slice*>(
            gpr_malloc(sb->capacity * sizeof(grpc_slice)));
        memcpy(sb->base_slices, sb->inlined, slice_count * sizeof(grpc_slice));
      } else {
        sb->base_slices = static_cast<grpc_slice*>(
            gpr_realloc(sb->base_slices, sb->capacity * sizeof(grpc_slice)));
      }

      sb->slices = sb->base_slices + slice_offset;
    }
  }
}

void grpc_slice_buffer_init(grpc_slice_buffer* sb) {
  sb->count = 0;
  sb->length = 0;
  sb->capacity = GRPC_SLICE_BUFFER_INLINE_ELEMENTS;
  sb->base_slices = sb->slices = sb->inlined;
}

void grpc_slice_buffer_destroy_internal(grpc_slice_buffer* sb) {
  grpc_slice_buffer_reset_and_unref_internal(sb);
  if (sb->base_slices != sb->inlined) {
    gpr_free(sb->base_slices);
  }
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
  grpc_slice* back;
  uint8_t* out;

  sb->length += n;

  if (sb->count == 0) goto add_new;
  back = &sb->slices[sb->count - 1];
  if (back->refcount) goto add_new;
  if ((back->data.inlined.length + n) > sizeof(back->data.inlined.bytes))
    goto add_new;
  out = back->data.inlined.bytes + back->data.inlined.length;
  back->data.inlined.length =
      static_cast<uint8_t>(back->data.inlined.length + n);
  return out;

add_new:
  maybe_embiggen(sb);
  back = &sb->slices[sb->count];
  sb->count++;
  back->refcount = nullptr;
  back->data.inlined.length = static_cast<uint8_t>(n);
  return back->data.inlined.bytes;
}

size_t grpc_slice_buffer_add_indexed(grpc_slice_buffer* sb, grpc_slice s) {
  size_t out = sb->count;
  maybe_embiggen(sb);
  sb->slices[out] = s;
  sb->length += GRPC_SLICE_LENGTH(s);
  sb->count = out + 1;
  return out;
}

void grpc_slice_buffer_add(grpc_slice_buffer* sb, grpc_slice s) {
  size_t n = sb->count;
  /* if both the last slice in the slice buffer and the slice being added
     are inlined (that is, that they carry their data inside the slice data
     structure), and the back slice is not full, then concatenate directly
     into the back slice, preventing many small slices being passed into
     writes */
  if (!s.refcount && n) {
    grpc_slice* back = &sb->slices[n - 1];
    if (!back->refcount &&
        back->data.inlined.length < GRPC_SLICE_INLINED_SIZE) {
      if (s.data.inlined.length + back->data.inlined.length <=
          GRPC_SLICE_INLINED_SIZE) {
        memcpy(back->data.inlined.bytes + back->data.inlined.length,
               s.data.inlined.bytes, s.data.inlined.length);
        back->data.inlined.length = static_cast<uint8_t>(
            back->data.inlined.length + s.data.inlined.length);
      } else {
        size_t cp1 = GRPC_SLICE_INLINED_SIZE - back->data.inlined.length;
        memcpy(back->data.inlined.bytes + back->data.inlined.length,
               s.data.inlined.bytes, cp1);
        back->data.inlined.length = GRPC_SLICE_INLINED_SIZE;
        maybe_embiggen(sb);
        back = &sb->slices[n];
        sb->count = n + 1;
        back->refcount = nullptr;
        back->data.inlined.length =
            static_cast<uint8_t>(s.data.inlined.length - cp1);
        memcpy(back->data.inlined.bytes, s.data.inlined.bytes + cp1,
               s.data.inlined.length - cp1);
      }
      sb->length += s.data.inlined.length;
      return; /* early out */
    }
  }
  grpc_slice_buffer_add_indexed(sb, s);
}

void grpc_slice_buffer_addn(grpc_slice_buffer* sb, grpc_slice* s, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    grpc_slice_buffer_add(sb, s[i]);
  }
}

void grpc_slice_buffer_pop(grpc_slice_buffer* sb) {
  if (sb->count != 0) {
    size_t count = --sb->count;
    sb->length -= GRPC_SLICE_LENGTH(sb->slices[count]);
  }
}

void grpc_slice_buffer_reset_and_unref_internal(grpc_slice_buffer* sb) {
  size_t i;
  for (i = 0; i < sb->count; i++) {
    grpc_slice_unref_internal(sb->slices[i]);
  }

  sb->count = 0;
  sb->length = 0;
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
  size_t a_offset = static_cast<size_t>(a->slices - a->base_slices);
  size_t b_offset = static_cast<size_t>(b->slices - b->base_slices);

  size_t a_count = a->count + a_offset;
  size_t b_count = b->count + b_offset;

  if (a->base_slices == a->inlined) {
    if (b->base_slices == b->inlined) {
      /* swap contents of inlined buffer */
      grpc_slice temp[GRPC_SLICE_BUFFER_INLINE_ELEMENTS];
      memcpy(temp, a->base_slices, a_count * sizeof(grpc_slice));
      memcpy(a->base_slices, b->base_slices, b_count * sizeof(grpc_slice));
      memcpy(b->base_slices, temp, a_count * sizeof(grpc_slice));
    } else {
      /* a is inlined, b is not - copy a inlined into b, fix pointers */
      a->base_slices = b->base_slices;
      b->base_slices = b->inlined;
      memcpy(b->base_slices, a->inlined, a_count * sizeof(grpc_slice));
    }
  } else if (b->base_slices == b->inlined) {
    /* b is inlined, a is not - copy b inlined int a, fix pointers */
    b->base_slices = a->base_slices;
    a->base_slices = a->inlined;
    memcpy(a->base_slices, b->inlined, b_count * sizeof(grpc_slice));
  } else {
    /* no inlining: easy swap */
    GPR_SWAP(grpc_slice*, a->base_slices, b->base_slices);
  }

  /* Update the slices pointers (cannot do a GPR_SWAP on slices fields here).
   * Also note that since the base_slices pointers are already swapped we need
   * use 'b_offset' for 'a->base_slices' and vice versa */
  a->slices = a->base_slices + b_offset;
  b->slices = b->base_slices + a_offset;

  /* base_slices and slices fields are correctly set. Swap all other fields */
  GPR_SWAP(size_t, a->count, b->count);
  GPR_SWAP(size_t, a->capacity, b->capacity);
  GPR_SWAP(size_t, a->length, b->length);
}

void grpc_slice_buffer_move_into(grpc_slice_buffer* src,
                                 grpc_slice_buffer* dst) {
  /* anything to move? */
  if (src->count == 0) {
    return;
  }
  /* anything in dst? */
  if (dst->count == 0) {
    grpc_slice_buffer_swap(src, dst);
    return;
  }
  /* both buffers have data - copy, and reset src */
  grpc_slice_buffer_addn(dst, src->slices, src->count);
  src->count = 0;
  src->length = 0;
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
  sb->slices--;
  sb->slices[0] = slice;
  sb->count++;
  sb->length += GRPC_SLICE_LENGTH(slice);
}
