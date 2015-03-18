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

#include "src/core/transport/stream_op.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <string.h>

/* Exponential growth function: Given x, return a larger x.
   Currently we grow by 1.5 times upon reallocation. */
#define GROW(x) (3 * (x) / 2)

void grpc_sopb_init(grpc_stream_op_buffer *sopb) {
  sopb->ops = sopb->inlined_ops;
  sopb->nops = 0;
  sopb->capacity = GRPC_SOPB_INLINE_ELEMENTS;
}

void grpc_sopb_destroy(grpc_stream_op_buffer *sopb) {
  grpc_stream_ops_unref_owned_objects(sopb->ops, sopb->nops);
  if (sopb->ops != sopb->inlined_ops) gpr_free(sopb->ops);
}

void grpc_sopb_reset(grpc_stream_op_buffer *sopb) {
  grpc_stream_ops_unref_owned_objects(sopb->ops, sopb->nops);
  sopb->nops = 0;
}

void grpc_sopb_swap(grpc_stream_op_buffer *a, grpc_stream_op_buffer *b) {
  grpc_stream_op_buffer temp = *a;
  *a = *b;
  *b = temp;

  if (a->ops == b->inlined_ops) {
    a->ops = a->inlined_ops;
  }
  if (b->ops == a->inlined_ops) {
    b->ops = b->inlined_ops;
  }
}

void grpc_stream_ops_unref_owned_objects(grpc_stream_op *ops, size_t nops) {
  size_t i;
  for (i = 0; i < nops; i++) {
    switch (ops[i].type) {
      case GRPC_OP_SLICE:
        gpr_slice_unref(ops[i].data.slice);
        break;
      case GRPC_OP_METADATA:
        grpc_mdelem_unref(ops[i].data.metadata);
        break;
      case GRPC_OP_FLOW_CTL_CB:
        ops[i].data.flow_ctl_cb.cb(ops[i].data.flow_ctl_cb.arg, GRPC_OP_ERROR);
        break;
      case GRPC_NO_OP:
      case GRPC_OP_DEADLINE:
      case GRPC_OP_METADATA_BOUNDARY:
      case GRPC_OP_BEGIN_MESSAGE:
        break;
    }
  }
}

static void expandto(grpc_stream_op_buffer *sopb, size_t new_capacity) {
  sopb->capacity = new_capacity;
  if (sopb->ops == sopb->inlined_ops) {
    sopb->ops = gpr_malloc(sizeof(grpc_stream_op) * new_capacity);
    memcpy(sopb->ops, sopb->inlined_ops, sopb->nops * sizeof(grpc_stream_op));
  } else {
    sopb->ops = gpr_realloc(sopb->ops, sizeof(grpc_stream_op) * new_capacity);
  }
}

static grpc_stream_op *add(grpc_stream_op_buffer *sopb) {
  grpc_stream_op *out;

  if (sopb->nops == sopb->capacity) {
    expandto(sopb, GROW(sopb->capacity));
  }
  out = sopb->ops + sopb->nops;
  sopb->nops++;
  return out;
}

void grpc_sopb_add_no_op(grpc_stream_op_buffer *sopb) {
  add(sopb)->type = GRPC_NO_OP;
}

void grpc_sopb_add_begin_message(grpc_stream_op_buffer *sopb, gpr_uint32 length,
                                 gpr_uint32 flags) {
  grpc_stream_op *op = add(sopb);
  op->type = GRPC_OP_BEGIN_MESSAGE;
  op->data.begin_message.length = length;
  op->data.begin_message.flags = flags;
}

void grpc_sopb_add_metadata_boundary(grpc_stream_op_buffer *sopb) {
  grpc_stream_op *op = add(sopb);
  op->type = GRPC_OP_METADATA_BOUNDARY;
}

void grpc_sopb_add_metadata(grpc_stream_op_buffer *sopb, grpc_mdelem *md) {
  grpc_stream_op *op = add(sopb);
  op->type = GRPC_OP_METADATA;
  op->data.metadata = md;
}

void grpc_sopb_add_deadline(grpc_stream_op_buffer *sopb,
                            gpr_timespec deadline) {
  grpc_stream_op *op = add(sopb);
  op->type = GRPC_OP_DEADLINE;
  op->data.deadline = deadline;
}

void grpc_sopb_add_slice(grpc_stream_op_buffer *sopb, gpr_slice slice) {
  grpc_stream_op *op = add(sopb);
  op->type = GRPC_OP_SLICE;
  op->data.slice = slice;
}

void grpc_sopb_add_flow_ctl_cb(grpc_stream_op_buffer *sopb,
                               void (*cb)(void *arg, grpc_op_error error),
                               void *arg) {
  grpc_stream_op *op = add(sopb);
  op->type = GRPC_OP_FLOW_CTL_CB;
  op->data.flow_ctl_cb.cb = cb;
  op->data.flow_ctl_cb.arg = arg;
}

void grpc_sopb_append(grpc_stream_op_buffer *sopb, grpc_stream_op *ops,
                      size_t nops) {
  size_t orig_nops = sopb->nops;
  size_t new_nops = orig_nops + nops;

  if (new_nops > sopb->capacity) {
    expandto(sopb, GPR_MAX(GROW(sopb->capacity), new_nops));
  }

  memcpy(sopb->ops + orig_nops, ops, sizeof(grpc_stream_op) * nops);
  sopb->nops = new_nops;
}
