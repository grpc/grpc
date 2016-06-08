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

#include "src/core/lib/channel/channel_stack.h"
#include <grpc/support/log.h>

#include <stdlib.h>
#include <string.h>

int grpc_trace_channel = 0;

/* Memory layouts.

   Channel stack is laid out as: {
     grpc_channel_stack stk;
     padding to GPR_MAX_ALIGNMENT
     grpc_channel_element[stk.count];
     per-filter memory, aligned to GPR_MAX_ALIGNMENT
   }

   Call stack is laid out as: {
     grpc_call_stack stk;
     padding to GPR_MAX_ALIGNMENT
     grpc_call_element[stk.count];
     per-filter memory, aligned to GPR_MAX_ALIGNMENT
   } */

/* Given a size, round up to the next multiple of sizeof(void*) */
#define ROUND_UP_TO_ALIGNMENT_SIZE(x) \
  (((x) + GPR_MAX_ALIGNMENT - 1u) & ~(GPR_MAX_ALIGNMENT - 1u))

size_t grpc_channel_stack_size(const grpc_channel_filter **filters,
                               size_t filter_count) {
  /* always need the header, and size for the channel elements */
  size_t size =
      ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(grpc_channel_stack)) +
      ROUND_UP_TO_ALIGNMENT_SIZE(filter_count * sizeof(grpc_channel_element));
  size_t i;

  GPR_ASSERT((GPR_MAX_ALIGNMENT & (GPR_MAX_ALIGNMENT - 1)) == 0 &&
             "GPR_MAX_ALIGNMENT must be a power of two");

  /* add the size for each filter */
  for (i = 0; i < filter_count; i++) {
    size += ROUND_UP_TO_ALIGNMENT_SIZE(filters[i]->sizeof_channel_data);
  }

  return size;
}

#define CHANNEL_ELEMS_FROM_STACK(stk)                                   \
  ((grpc_channel_element *)((char *)(stk) + ROUND_UP_TO_ALIGNMENT_SIZE( \
                                                sizeof(grpc_channel_stack))))

#define CALL_ELEMS_FROM_STACK(stk)       \
  ((grpc_call_element *)((char *)(stk) + \
                         ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(grpc_call_stack))))

grpc_channel_element *grpc_channel_stack_element(
    grpc_channel_stack *channel_stack, size_t index) {
  return CHANNEL_ELEMS_FROM_STACK(channel_stack) + index;
}

grpc_channel_element *grpc_channel_stack_last_element(
    grpc_channel_stack *channel_stack) {
  return grpc_channel_stack_element(channel_stack, channel_stack->count - 1);
}

grpc_call_element *grpc_call_stack_element(grpc_call_stack *call_stack,
                                           size_t index) {
  return CALL_ELEMS_FROM_STACK(call_stack) + index;
}

void grpc_channel_stack_init(grpc_exec_ctx *exec_ctx, int initial_refs,
                             grpc_iomgr_cb_func destroy, void *destroy_arg,
                             const grpc_channel_filter **filters,
                             size_t filter_count,
                             const grpc_channel_args *channel_args,
                             const char *name, grpc_channel_stack *stack) {
  size_t call_size =
      ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(grpc_call_stack)) +
      ROUND_UP_TO_ALIGNMENT_SIZE(filter_count * sizeof(grpc_call_element));
  grpc_channel_element *elems;
  grpc_channel_element_args args;
  char *user_data;
  size_t i;

  stack->count = filter_count;
  GRPC_STREAM_REF_INIT(&stack->refcount, initial_refs, destroy, destroy_arg,
                       name);
  elems = CHANNEL_ELEMS_FROM_STACK(stack);
  user_data =
      ((char *)elems) +
      ROUND_UP_TO_ALIGNMENT_SIZE(filter_count * sizeof(grpc_channel_element));

  /* init per-filter data */
  for (i = 0; i < filter_count; i++) {
    args.channel_stack = stack;
    args.channel_args = channel_args;
    args.is_first = i == 0;
    args.is_last = i == (filter_count - 1);
    elems[i].filter = filters[i];
    elems[i].channel_data = user_data;
    elems[i].filter->init_channel_elem(exec_ctx, &elems[i], &args);
    user_data += ROUND_UP_TO_ALIGNMENT_SIZE(filters[i]->sizeof_channel_data);
    call_size += ROUND_UP_TO_ALIGNMENT_SIZE(filters[i]->sizeof_call_data);
  }

  GPR_ASSERT(user_data > (char *)stack);
  GPR_ASSERT((uintptr_t)(user_data - (char *)stack) ==
             grpc_channel_stack_size(filters, filter_count));

  stack->call_stack_size = call_size;
}

void grpc_channel_stack_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_channel_stack *stack) {
  grpc_channel_element *channel_elems = CHANNEL_ELEMS_FROM_STACK(stack);
  size_t count = stack->count;
  size_t i;

  /* destroy per-filter data */
  for (i = 0; i < count; i++) {
    channel_elems[i].filter->destroy_channel_elem(exec_ctx, &channel_elems[i]);
  }
}

void grpc_call_stack_init(grpc_exec_ctx *exec_ctx,
                          grpc_channel_stack *channel_stack, int initial_refs,
                          grpc_iomgr_cb_func destroy, void *destroy_arg,
                          grpc_call_context_element *context,
                          const void *transport_server_data,
                          grpc_call_stack *call_stack) {
  grpc_channel_element *channel_elems = CHANNEL_ELEMS_FROM_STACK(channel_stack);
  grpc_call_element_args args;
  size_t count = channel_stack->count;
  grpc_call_element *call_elems;
  char *user_data;
  size_t i;

  call_stack->count = count;
  GRPC_STREAM_REF_INIT(&call_stack->refcount, initial_refs, destroy,
                       destroy_arg, "CALL_STACK");
  call_elems = CALL_ELEMS_FROM_STACK(call_stack);
  user_data = ((char *)call_elems) +
              ROUND_UP_TO_ALIGNMENT_SIZE(count * sizeof(grpc_call_element));

  /* init per-filter data */
  for (i = 0; i < count; i++) {
    args.call_stack = call_stack;
    args.server_transport_data = transport_server_data;
    args.context = context;
    call_elems[i].filter = channel_elems[i].filter;
    call_elems[i].channel_data = channel_elems[i].channel_data;
    call_elems[i].call_data = user_data;
    call_elems[i].filter->init_call_elem(exec_ctx, &call_elems[i], &args);
    user_data +=
        ROUND_UP_TO_ALIGNMENT_SIZE(call_elems[i].filter->sizeof_call_data);
  }
}

void grpc_call_stack_set_pollset_or_pollset_set(grpc_exec_ctx *exec_ctx,
                                                grpc_call_stack *call_stack,
                                                grpc_polling_entity *pollent) {
  size_t count = call_stack->count;
  grpc_call_element *call_elems;
  char *user_data;
  size_t i;

  call_elems = CALL_ELEMS_FROM_STACK(call_stack);
  user_data = ((char *)call_elems) +
              ROUND_UP_TO_ALIGNMENT_SIZE(count * sizeof(grpc_call_element));

  /* init per-filter data */
  for (i = 0; i < count; i++) {
    call_elems[i].filter->set_pollset_or_pollset_set(exec_ctx, &call_elems[i],
                                                     pollent);
    user_data +=
        ROUND_UP_TO_ALIGNMENT_SIZE(call_elems[i].filter->sizeof_call_data);
  }
}

void grpc_call_stack_ignore_set_pollset_or_pollset_set(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_polling_entity *pollent) {}

void grpc_call_stack_destroy(grpc_exec_ctx *exec_ctx, grpc_call_stack *stack,
                             const grpc_call_stats *call_stats,
                             void *and_free_memory) {
  grpc_call_element *elems = CALL_ELEMS_FROM_STACK(stack);
  size_t count = stack->count;
  size_t i;

  /* destroy per-filter data */
  for (i = 0; i < count; i++) {
    elems[i].filter->destroy_call_elem(exec_ctx, &elems[i], call_stats,
                                       i == count - 1 ? and_free_memory : NULL);
  }
}

void grpc_call_next_op(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                       grpc_transport_stream_op *op) {
  grpc_call_element *next_elem = elem + 1;
  next_elem->filter->start_transport_stream_op(exec_ctx, next_elem, op);
}

char *grpc_call_next_get_peer(grpc_exec_ctx *exec_ctx,
                              grpc_call_element *elem) {
  grpc_call_element *next_elem = elem + 1;
  return next_elem->filter->get_peer(exec_ctx, next_elem);
}

void grpc_channel_next_op(grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
                          grpc_transport_op *op) {
  grpc_channel_element *next_elem = elem + 1;
  next_elem->filter->start_transport_op(exec_ctx, next_elem, op);
}

grpc_channel_stack *grpc_channel_stack_from_top_element(
    grpc_channel_element *elem) {
  return (grpc_channel_stack *)((char *)(elem)-ROUND_UP_TO_ALIGNMENT_SIZE(
      sizeof(grpc_channel_stack)));
}

grpc_call_stack *grpc_call_stack_from_top_element(grpc_call_element *elem) {
  return (grpc_call_stack *)((char *)(elem)-ROUND_UP_TO_ALIGNMENT_SIZE(
      sizeof(grpc_call_stack)));
}

void grpc_call_element_send_cancel(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *cur_elem) {
  grpc_transport_stream_op op;
  memset(&op, 0, sizeof(op));
  op.cancel_with_status = GRPC_STATUS_CANCELLED;
  grpc_call_next_op(exec_ctx, cur_elem, &op);
}
