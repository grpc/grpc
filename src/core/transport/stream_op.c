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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/profiling/timers.h"

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
  GPR_SWAP(size_t, a->nops, b->nops);
  GPR_SWAP(size_t, a->capacity, b->capacity);

  if (a->ops == a->inlined_ops) {
    if (b->ops == b->inlined_ops) {
      /* swap contents of inlined buffer */
      grpc_stream_op temp[GRPC_SOPB_INLINE_ELEMENTS];
      memcpy(temp, a->ops, b->nops * sizeof(grpc_stream_op));
      memcpy(a->ops, b->ops, a->nops * sizeof(grpc_stream_op));
      memcpy(b->ops, temp, b->nops * sizeof(grpc_stream_op));
    } else {
      /* a is inlined, b is not - copy a inlined into b, fix pointers */
      a->ops = b->ops;
      b->ops = b->inlined_ops;
      memcpy(b->ops, a->inlined_ops, b->nops * sizeof(grpc_stream_op));
    }
  } else if (b->ops == b->inlined_ops) {
    /* b is inlined, a is not - copy b inlined int a, fix pointers */
    b->ops = a->ops;
    a->ops = a->inlined_ops;
    memcpy(a->ops, b->inlined_ops, a->nops * sizeof(grpc_stream_op));
  } else {
    /* no inlining: easy swap */
    GPR_SWAP(grpc_stream_op *, a->ops, b->ops);
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
        grpc_metadata_batch_destroy(&ops[i].data.metadata);
        break;
      case GRPC_NO_OP:
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

  GPR_ASSERT(sopb->nops <= sopb->capacity);
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

void grpc_sopb_add_metadata(grpc_stream_op_buffer *sopb,
                            grpc_metadata_batch b) {
  grpc_stream_op *op = add(sopb);
  op->type = GRPC_OP_METADATA;
  op->data.metadata = b;
}

void grpc_sopb_add_slice(grpc_stream_op_buffer *sopb, gpr_slice slice) {
  grpc_stream_op *op = add(sopb);
  op->type = GRPC_OP_SLICE;
  op->data.slice = slice;
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

void grpc_sopb_move_to(grpc_stream_op_buffer *src, grpc_stream_op_buffer *dst) {
  if (src->nops == 0) {
    return;
  }
  if (dst->nops == 0) {
    grpc_sopb_swap(src, dst);
    return;
  }
  grpc_sopb_append(dst, src->ops, src->nops);
  src->nops = 0;
}

static void assert_valid_list(grpc_mdelem_list *list) {
#ifndef NDEBUG
  grpc_linked_mdelem *l;

  GPR_ASSERT((list->head == NULL) == (list->tail == NULL));
  if (!list->head) return;
  GPR_ASSERT(list->head->prev == NULL);
  GPR_ASSERT(list->tail->next == NULL);
  GPR_ASSERT((list->head == list->tail) == (list->head->next == NULL));

  for (l = list->head; l; l = l->next) {
    GPR_ASSERT(l->md);
    GPR_ASSERT((l->prev == NULL) == (l == list->head));
    GPR_ASSERT((l->next == NULL) == (l == list->tail));
    if (l->next) GPR_ASSERT(l->next->prev == l);
    if (l->prev) GPR_ASSERT(l->prev->next == l);
  }
#endif /* NDEBUG */
}

#ifndef NDEBUG
void grpc_metadata_batch_assert_ok(grpc_metadata_batch *batch) {
  assert_valid_list(&batch->list);
  assert_valid_list(&batch->garbage);
}
#endif /* NDEBUG */

void grpc_metadata_batch_init(grpc_metadata_batch *batch) {
  batch->list.head = batch->list.tail = batch->garbage.head =
      batch->garbage.tail = NULL;
  batch->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
}

void grpc_metadata_batch_destroy(grpc_metadata_batch *batch) {
  grpc_linked_mdelem *l;
  for (l = batch->list.head; l; l = l->next) {
    GRPC_MDELEM_UNREF(l->md);
  }
  for (l = batch->garbage.head; l; l = l->next) {
    GRPC_MDELEM_UNREF(l->md);
  }
}

void grpc_metadata_batch_add_head(grpc_metadata_batch *batch,
                                  grpc_linked_mdelem *storage,
                                  grpc_mdelem *elem_to_add) {
  GPR_ASSERT(elem_to_add);
  storage->md = elem_to_add;
  grpc_metadata_batch_link_head(batch, storage);
}

static void link_head(grpc_mdelem_list *list, grpc_linked_mdelem *storage) {
  assert_valid_list(list);
  GPR_ASSERT(storage->md);
  storage->prev = NULL;
  storage->next = list->head;
  if (list->head != NULL) {
    list->head->prev = storage;
  } else {
    list->tail = storage;
  }
  list->head = storage;
  assert_valid_list(list);
}

void grpc_metadata_batch_link_head(grpc_metadata_batch *batch,
                                   grpc_linked_mdelem *storage) {
  link_head(&batch->list, storage);
}

void grpc_metadata_batch_add_tail(grpc_metadata_batch *batch,
                                  grpc_linked_mdelem *storage,
                                  grpc_mdelem *elem_to_add) {
  GPR_ASSERT(elem_to_add);
  storage->md = elem_to_add;
  grpc_metadata_batch_link_tail(batch, storage);
}

static void link_tail(grpc_mdelem_list *list, grpc_linked_mdelem *storage) {
  assert_valid_list(list);
  GPR_ASSERT(storage->md);
  storage->prev = list->tail;
  storage->next = NULL;
  storage->reserved = NULL;
  if (list->tail != NULL) {
    list->tail->next = storage;
  } else {
    list->head = storage;
  }
  list->tail = storage;
  assert_valid_list(list);
}

void grpc_metadata_batch_link_tail(grpc_metadata_batch *batch,
                                   grpc_linked_mdelem *storage) {
  link_tail(&batch->list, storage);
}

void grpc_metadata_batch_merge(grpc_metadata_batch *target,
                               grpc_metadata_batch *to_add) {
  grpc_linked_mdelem *l;
  grpc_linked_mdelem *next;
  for (l = to_add->list.head; l; l = next) {
    next = l->next;
    link_tail(&target->list, l);
  }
  for (l = to_add->garbage.head; l; l = next) {
    next = l->next;
    link_tail(&target->garbage, l);
  }
}

void grpc_metadata_batch_move(grpc_metadata_batch *dst,
                              grpc_metadata_batch *src) {
  *dst = *src;
  memset(src, 0, sizeof(grpc_metadata_batch));
}

void grpc_metadata_batch_filter(grpc_metadata_batch *batch,
                                grpc_mdelem *(*filter)(void *user_data,
                                                       grpc_mdelem *elem),
                                void *user_data) {
  grpc_linked_mdelem *l;
  grpc_linked_mdelem *next;

  GRPC_TIMER_BEGIN("grpc_metadata_batch_filter", 0);

  assert_valid_list(&batch->list);
  assert_valid_list(&batch->garbage);
  for (l = batch->list.head; l; l = next) {
    grpc_mdelem *orig = l->md;
    grpc_mdelem *filt = filter(user_data, orig);
    next = l->next;
    if (filt == NULL) {
      if (l->prev) {
        l->prev->next = l->next;
      }
      if (l->next) {
        l->next->prev = l->prev;
      }
      if (batch->list.head == l) {
        batch->list.head = l->next;
      }
      if (batch->list.tail == l) {
        batch->list.tail = l->prev;
      }
      assert_valid_list(&batch->list);
      link_head(&batch->garbage, l);
    } else if (filt != orig) {
      GRPC_MDELEM_UNREF(orig);
      l->md = filt;
    }
  }
  assert_valid_list(&batch->list);
  assert_valid_list(&batch->garbage);

  GRPC_TIMER_END("grpc_metadata_batch_filter", 0);
}
