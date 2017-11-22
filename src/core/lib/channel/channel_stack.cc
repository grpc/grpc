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

#include "src/core/lib/channel/channel_stack.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <stdlib.h>
#include <string.h>

grpc_core::TraceFlag grpc_trace_channel(false, "channel");

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

size_t grpc_channel_stack_size(const grpc_channel_filter** filters,
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

#define CHANNEL_ELEMS_FROM_STACK(stk)                                 \
  ((grpc_channel_element*)((char*)(stk) + ROUND_UP_TO_ALIGNMENT_SIZE( \
                                              sizeof(grpc_channel_stack))))

#define CALL_ELEMS_FROM_STACK(stk)     \
  ((grpc_call_element*)((char*)(stk) + \
                        ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(grpc_call_stack))))

grpc_channel_element* grpc_channel_stack_element(
    grpc_channel_stack* channel_stack, size_t index) {
  return CHANNEL_ELEMS_FROM_STACK(channel_stack) + index;
}

grpc_channel_element* grpc_channel_stack_last_element(
    grpc_channel_stack* channel_stack) {
  return grpc_channel_stack_element(channel_stack, channel_stack->count - 1);
}

grpc_call_element* grpc_call_stack_element(grpc_call_stack* call_stack,
                                           size_t index) {
  return CALL_ELEMS_FROM_STACK(call_stack) + index;
}

grpc_error* grpc_channel_stack_init(
    int initial_refs, grpc_iomgr_cb_func destroy, void* destroy_arg,
    const grpc_channel_filter** filters, size_t filter_count,
    const grpc_channel_args* channel_args, grpc_transport* optional_transport,
    const char* name, grpc_channel_stack* stack) {
  size_t call_size =
      ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(grpc_call_stack)) +
      ROUND_UP_TO_ALIGNMENT_SIZE(filter_count * sizeof(grpc_call_element));
  grpc_channel_element* elems;
  grpc_channel_element_args args;
  char* user_data;
  size_t i;

  stack->count = filter_count;
  GRPC_STREAM_REF_INIT(&stack->refcount, initial_refs, destroy, destroy_arg,
                       name);
  elems = CHANNEL_ELEMS_FROM_STACK(stack);
  user_data = ((char*)elems) + ROUND_UP_TO_ALIGNMENT_SIZE(
                                   filter_count * sizeof(grpc_channel_element));

  /* init per-filter data */
  grpc_error* first_error = GRPC_ERROR_NONE;
  for (i = 0; i < filter_count; i++) {
    args.channel_stack = stack;
    args.channel_args = channel_args;
    args.optional_transport = optional_transport;
    args.is_first = i == 0;
    args.is_last = i == (filter_count - 1);
    elems[i].filter = filters[i];
    elems[i].channel_data = user_data;
    grpc_error* error = elems[i].filter->init_channel_elem(&elems[i], &args);
    if (error != GRPC_ERROR_NONE) {
      if (first_error == GRPC_ERROR_NONE) {
        first_error = error;
      } else {
        GRPC_ERROR_UNREF(error);
      }
    }
    user_data += ROUND_UP_TO_ALIGNMENT_SIZE(filters[i]->sizeof_channel_data);
    call_size += ROUND_UP_TO_ALIGNMENT_SIZE(filters[i]->sizeof_call_data);
  }

  GPR_ASSERT(user_data > (char*)stack);
  GPR_ASSERT((uintptr_t)(user_data - (char*)stack) ==
             grpc_channel_stack_size(filters, filter_count));

  stack->call_stack_size = call_size;
  return first_error;
}

void grpc_channel_stack_destroy(grpc_channel_stack* stack) {
  grpc_channel_element* channel_elems = CHANNEL_ELEMS_FROM_STACK(stack);
  size_t count = stack->count;
  size_t i;

  /* destroy per-filter data */
  for (i = 0; i < count; i++) {
    channel_elems[i].filter->destroy_channel_elem(&channel_elems[i]);
  }
}

grpc_error* grpc_call_stack_init(grpc_channel_stack* channel_stack,
                                 int initial_refs, grpc_iomgr_cb_func destroy,
                                 void* destroy_arg,
                                 const grpc_call_element_args* elem_args) {
  grpc_channel_element* channel_elems = CHANNEL_ELEMS_FROM_STACK(channel_stack);
  size_t count = channel_stack->count;
  grpc_call_element* call_elems;
  char* user_data;
  size_t i;

  elem_args->call_stack->count = count;
  GRPC_STREAM_REF_INIT(&elem_args->call_stack->refcount, initial_refs, destroy,
                       destroy_arg, "CALL_STACK");
  call_elems = CALL_ELEMS_FROM_STACK(elem_args->call_stack);
  user_data = ((char*)call_elems) +
              ROUND_UP_TO_ALIGNMENT_SIZE(count * sizeof(grpc_call_element));

  /* init per-filter data */
  grpc_error* first_error = GRPC_ERROR_NONE;
  for (i = 0; i < count; i++) {
    call_elems[i].filter = channel_elems[i].filter;
    call_elems[i].channel_data = channel_elems[i].channel_data;
    call_elems[i].call_data = user_data;
    grpc_error* error =
        call_elems[i].filter->init_call_elem(&call_elems[i], elem_args);
    if (error != GRPC_ERROR_NONE) {
      if (first_error == GRPC_ERROR_NONE) {
        first_error = error;
      } else {
        GRPC_ERROR_UNREF(error);
      }
    }
    user_data +=
        ROUND_UP_TO_ALIGNMENT_SIZE(call_elems[i].filter->sizeof_call_data);
  }
  return first_error;
}

void grpc_call_stack_set_pollset_or_pollset_set(grpc_call_stack* call_stack,
                                                grpc_polling_entity* pollent) {
  size_t count = call_stack->count;
  grpc_call_element* call_elems;
  char* user_data;
  size_t i;

  call_elems = CALL_ELEMS_FROM_STACK(call_stack);
  user_data = ((char*)call_elems) +
              ROUND_UP_TO_ALIGNMENT_SIZE(count * sizeof(grpc_call_element));

  /* init per-filter data */
  for (i = 0; i < count; i++) {
    call_elems[i].filter->set_pollset_or_pollset_set(&call_elems[i], pollent);
    user_data +=
        ROUND_UP_TO_ALIGNMENT_SIZE(call_elems[i].filter->sizeof_call_data);
  }
}

void grpc_call_stack_ignore_set_pollset_or_pollset_set(
    grpc_call_element* elem, grpc_polling_entity* pollent) {}

void grpc_call_stack_destroy(grpc_call_stack* stack,
                             const grpc_call_final_info* final_info,
                             grpc_closure* then_schedule_closure) {
  grpc_call_element* elems = CALL_ELEMS_FROM_STACK(stack);
  size_t count = stack->count;
  size_t i;

  /* destroy per-filter data */
  for (i = 0; i < count; i++) {
    elems[i].filter->destroy_call_elem(
        &elems[i], final_info,
        i == count - 1 ? then_schedule_closure : nullptr);
  }
}

void grpc_call_next_op(grpc_call_element* elem,
                       grpc_transport_stream_op_batch* op) {
  grpc_call_element* next_elem = elem + 1;
  GRPC_CALL_LOG_OP(GPR_INFO, next_elem, op);
  next_elem->filter->start_transport_stream_op_batch(next_elem, op);
}

void grpc_channel_next_get_info(grpc_channel_element* elem,
                                const grpc_channel_info* channel_info) {
  grpc_channel_element* next_elem = elem + 1;
  next_elem->filter->get_channel_info(next_elem, channel_info);
}

void grpc_channel_next_op(grpc_channel_element* elem, grpc_transport_op* op) {
  grpc_channel_element* next_elem = elem + 1;
  next_elem->filter->start_transport_op(next_elem, op);
}

grpc_channel_stack* grpc_channel_stack_from_top_element(
    grpc_channel_element* elem) {
  return (grpc_channel_stack*)((char*)(elem)-ROUND_UP_TO_ALIGNMENT_SIZE(
      sizeof(grpc_channel_stack)));
}

grpc_call_stack* grpc_call_stack_from_top_element(grpc_call_element* elem) {
  return (grpc_call_stack*)((char*)(elem)-ROUND_UP_TO_ALIGNMENT_SIZE(
      sizeof(grpc_call_stack)));
}
