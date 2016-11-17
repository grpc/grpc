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

#include "src/core/lib/transport/metadata_batch.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"

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
}
#endif /* NDEBUG */

void grpc_metadata_batch_init(grpc_metadata_batch *batch) {
  batch->list.head = batch->list.tail = NULL;
  batch->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
}

void grpc_metadata_batch_destroy(grpc_exec_ctx *exec_ctx,
                                 grpc_metadata_batch *batch) {
  grpc_linked_mdelem *l;
  for (l = batch->list.head; l; l = l->next) {
    GRPC_MDELEM_UNREF(exec_ctx, l->md);
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

void grpc_metadata_batch_move(grpc_metadata_batch *dst,
                              grpc_metadata_batch *src) {
  *dst = *src;
  memset(src, 0, sizeof(grpc_metadata_batch));
}

void grpc_metadata_batch_filter(grpc_exec_ctx *exec_ctx,
                                grpc_metadata_batch *batch,
                                grpc_mdelem *(*filter)(grpc_exec_ctx *exec_ctx,
                                                       void *user_data,
                                                       grpc_mdelem *elem),
                                void *user_data) {
  grpc_linked_mdelem *l;
  grpc_linked_mdelem *next;

  GPR_TIMER_BEGIN("grpc_metadata_batch_filter", 0);

  assert_valid_list(&batch->list);
  for (l = batch->list.head; l; l = next) {
    grpc_mdelem *orig = l->md;
    grpc_mdelem *filt = filter(exec_ctx, user_data, orig);
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
      GRPC_MDELEM_UNREF(exec_ctx, l->md);
    } else if (filt != orig) {
      GRPC_MDELEM_UNREF(exec_ctx, orig);
      l->md = filt;
    }
  }
  assert_valid_list(&batch->list);

  GPR_TIMER_END("grpc_metadata_batch_filter", 0);
}

static grpc_mdelem *no_metadata_for_you(grpc_exec_ctx *exec_ctx,
                                        void *user_data, grpc_mdelem *elem) {
  return NULL;
}

void grpc_metadata_batch_clear(grpc_exec_ctx *exec_ctx,
                               grpc_metadata_batch *batch) {
  batch->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
  grpc_metadata_batch_filter(exec_ctx, batch, no_metadata_for_you, NULL);
}

bool grpc_metadata_batch_is_empty(grpc_metadata_batch *batch) {
  return batch->list.head == NULL &&
         gpr_time_cmp(gpr_inf_future(batch->deadline.clock_type),
                      batch->deadline) == 0;
}

size_t grpc_metadata_batch_size(grpc_metadata_batch *batch) {
  size_t size = 0;
  for (grpc_linked_mdelem *elem = batch->list.head; elem != NULL;
       elem = elem->next) {
    size += GRPC_MDELEM_LENGTH(elem->md);
  }
  return size;
}
