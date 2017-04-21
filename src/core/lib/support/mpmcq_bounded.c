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

#include "src/core/lib/support/mpmcq_bounded.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

void gpr_mpmcq_bounded_init(gpr_mpmcq_bounded *q, size_t buffer_size) {
  /* Buffer size should be a power of 2 */
  GPR_ASSERT((buffer_size & (buffer_size - 1)) == 0);

  /* TODO: sreek - Remove this alloc. Accept the buffer pointer as input */
  q->buffer = gpr_malloc(buffer_size * sizeof(gpr_mpmcq_cell));
  q->buffer_mask = buffer_size - 1;
  for (size_t i = 0; i <= buffer_size; i++) {
    gpr_atm_no_barrier_store(&q->buffer[i].sequence, i);
  }

  gpr_atm_no_barrier_store(&q->enqueue_pos, 0);
  gpr_atm_no_barrier_store(&q->dequeue_pos, 0);
}

void gpr_mpmcq_bounded_destroy(gpr_mpmcq_bounded *q) {
  /* TODO: sreek - This should be removed once we remove the alloc in
   * gpr_mpmc_bounded_init() */
  gpr_free(q->buffer);
}

bool gpr_mpmcq_bounded_push(gpr_mpmcq_bounded *q, void *data) {
  gpr_mpmcq_cell *cell;
  size_t seq;
  size_t pos = (size_t)gpr_atm_no_barrier_load(&q->enqueue_pos);
  for (;;) {
    cell = &q->buffer[pos & q->buffer_mask];
    seq = (size_t)gpr_atm_acq_load(&cell->sequence);
    intptr_t diff = (intptr_t)seq - (intptr_t)pos;
    if (diff == 0) {
      if (gpr_atm_no_barrier_cas(&q->enqueue_pos, (gpr_atm)pos,
                                 (gpr_atm)pos + 1)) {
        break;
      }
    } else if (diff < 0) {
      return false;
    } else {
      pos = (size_t)gpr_atm_no_barrier_load(&q->enqueue_pos);
    }
  }

  cell->data = data;
  gpr_atm_rel_store(&cell->sequence, pos + 1);
  return true;
}

bool gpr_mpmcq_bounded_pop(gpr_mpmcq_bounded *q, void **data) {
  gpr_mpmcq_cell *cell;
  size_t seq;
  size_t pos = (size_t)gpr_atm_no_barrier_load(&q->dequeue_pos);
  for (;;) {
    cell = &q->buffer[pos & q->buffer_mask];
    seq = (size_t)gpr_atm_acq_load(&cell->sequence);
    intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
    if (diff == 0) {
      if (gpr_atm_no_barrier_cas(&q->dequeue_pos, (gpr_atm)pos,
                                 (gpr_atm)pos + 1)) {
        break;
      }
    } else if (diff < 0) {
      return false;
    } else {
      pos = (size_t)gpr_atm_no_barrier_load(&q->dequeue_pos);
    }
  }

  *data = cell->data;
  gpr_atm_rel_store(&cell->sequence, pos + q->buffer_mask + 1);
  return true;
}
