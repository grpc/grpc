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

#include "src/core/lib/transport/metadata_batch.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

static void assert_valid_list(grpc_mdelem_list *list) {
#ifndef NDEBUG
  grpc_linked_mdelem *l;

  GPR_ASSERT((list->head == NULL) == (list->tail == NULL));
  if (!list->head) return;
  GPR_ASSERT(list->head->prev == NULL);
  GPR_ASSERT(list->tail->next == NULL);
  GPR_ASSERT((list->head == list->tail) == (list->head->next == NULL));

  size_t verified_count = 0;
  for (l = list->head; l; l = l->next) {
    GPR_ASSERT(!GRPC_MDISNULL(l->md));
    GPR_ASSERT((l->prev == NULL) == (l == list->head));
    GPR_ASSERT((l->next == NULL) == (l == list->tail));
    if (l->next) GPR_ASSERT(l->next->prev == l);
    if (l->prev) GPR_ASSERT(l->prev->next == l);
    verified_count++;
  }
  GPR_ASSERT(list->count == verified_count);
#endif /* NDEBUG */
}

static void assert_valid_callouts(grpc_exec_ctx *exec_ctx,
                                  grpc_metadata_batch *batch) {
#ifndef NDEBUG
  for (grpc_linked_mdelem *l = batch->list.head; l != NULL; l = l->next) {
    grpc_slice key_interned = grpc_slice_intern(GRPC_MDKEY(l->md));
    grpc_metadata_batch_callouts_index callout_idx =
        GRPC_BATCH_INDEX_OF(key_interned);
    if (callout_idx != GRPC_BATCH_CALLOUTS_COUNT) {
      GPR_ASSERT(batch->idx.array[callout_idx] == l);
    }
    grpc_slice_unref_internal(exec_ctx, key_interned);
  }
#endif
}

#ifndef NDEBUG
void grpc_metadata_batch_assert_ok(grpc_metadata_batch *batch) {
  assert_valid_list(&batch->list);
}
#endif /* NDEBUG */

void grpc_metadata_batch_init(grpc_metadata_batch *batch) {
  memset(batch, 0, sizeof(*batch));
  batch->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
}

void grpc_metadata_batch_destroy(grpc_exec_ctx *exec_ctx,
                                 grpc_metadata_batch *batch) {
  grpc_linked_mdelem *l;
  for (l = batch->list.head; l; l = l->next) {
    GRPC_MDELEM_UNREF(exec_ctx, l->md);
  }
}

grpc_error *grpc_attach_md_to_error(grpc_error *src, grpc_mdelem md) {
  grpc_error *out = grpc_error_set_str(
      grpc_error_set_str(src, GRPC_ERROR_STR_KEY,
                         grpc_slice_ref_internal(GRPC_MDKEY(md))),
      GRPC_ERROR_STR_VALUE, grpc_slice_ref_internal(GRPC_MDVALUE(md)));
  return out;
}

static grpc_error *maybe_link_callout(grpc_metadata_batch *batch,
                                      grpc_linked_mdelem *storage)
    GRPC_MUST_USE_RESULT;

static grpc_error *maybe_link_callout(grpc_metadata_batch *batch,
                                      grpc_linked_mdelem *storage) {
  grpc_metadata_batch_callouts_index idx =
      GRPC_BATCH_INDEX_OF(GRPC_MDKEY(storage->md));
  if (idx == GRPC_BATCH_CALLOUTS_COUNT) {
    return GRPC_ERROR_NONE;
  }
  if (batch->idx.array[idx] == NULL) {
    if (grpc_static_callout_is_default[idx]) ++batch->list.default_count;
    batch->idx.array[idx] = storage;
    return GRPC_ERROR_NONE;
  }
  return grpc_attach_md_to_error(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unallowed duplicate metadata"),
      storage->md);
}

static void maybe_unlink_callout(grpc_metadata_batch *batch,
                                 grpc_linked_mdelem *storage) {
  grpc_metadata_batch_callouts_index idx =
      GRPC_BATCH_INDEX_OF(GRPC_MDKEY(storage->md));
  if (idx == GRPC_BATCH_CALLOUTS_COUNT) {
    return;
  }
  if (grpc_static_callout_is_default[idx]) --batch->list.default_count;
  GPR_ASSERT(batch->idx.array[idx] != NULL);
  batch->idx.array[idx] = NULL;
}

grpc_error *grpc_metadata_batch_add_head(grpc_exec_ctx *exec_ctx,
                                         grpc_metadata_batch *batch,
                                         grpc_linked_mdelem *storage,
                                         grpc_mdelem elem_to_add) {
  GPR_ASSERT(!GRPC_MDISNULL(elem_to_add));
  storage->md = elem_to_add;
  return grpc_metadata_batch_link_head(exec_ctx, batch, storage);
}

static void link_head(grpc_mdelem_list *list, grpc_linked_mdelem *storage) {
  assert_valid_list(list);
  GPR_ASSERT(!GRPC_MDISNULL(storage->md));
  storage->prev = NULL;
  storage->next = list->head;
  if (list->head != NULL) {
    list->head->prev = storage;
  } else {
    list->tail = storage;
  }
  list->head = storage;
  list->count++;
  assert_valid_list(list);
}

grpc_error *grpc_metadata_batch_link_head(grpc_exec_ctx *exec_ctx,
                                          grpc_metadata_batch *batch,
                                          grpc_linked_mdelem *storage) {
  assert_valid_callouts(exec_ctx, batch);
  grpc_error *err = maybe_link_callout(batch, storage);
  if (err != GRPC_ERROR_NONE) {
    assert_valid_callouts(exec_ctx, batch);
    return err;
  }
  link_head(&batch->list, storage);
  assert_valid_callouts(exec_ctx, batch);
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_metadata_batch_add_tail(grpc_exec_ctx *exec_ctx,
                                         grpc_metadata_batch *batch,
                                         grpc_linked_mdelem *storage,
                                         grpc_mdelem elem_to_add) {
  GPR_ASSERT(!GRPC_MDISNULL(elem_to_add));
  storage->md = elem_to_add;
  return grpc_metadata_batch_link_tail(exec_ctx, batch, storage);
}

static void link_tail(grpc_mdelem_list *list, grpc_linked_mdelem *storage) {
  assert_valid_list(list);
  GPR_ASSERT(!GRPC_MDISNULL(storage->md));
  storage->prev = list->tail;
  storage->next = NULL;
  storage->reserved = NULL;
  if (list->tail != NULL) {
    list->tail->next = storage;
  } else {
    list->head = storage;
  }
  list->tail = storage;
  list->count++;
  assert_valid_list(list);
}

grpc_error *grpc_metadata_batch_link_tail(grpc_exec_ctx *exec_ctx,
                                          grpc_metadata_batch *batch,
                                          grpc_linked_mdelem *storage) {
  assert_valid_callouts(exec_ctx, batch);
  grpc_error *err = maybe_link_callout(batch, storage);
  if (err != GRPC_ERROR_NONE) {
    assert_valid_callouts(exec_ctx, batch);
    return err;
  }
  link_tail(&batch->list, storage);
  assert_valid_callouts(exec_ctx, batch);
  return GRPC_ERROR_NONE;
}

static void unlink_storage(grpc_mdelem_list *list,
                           grpc_linked_mdelem *storage) {
  assert_valid_list(list);
  if (storage->prev != NULL) {
    storage->prev->next = storage->next;
  } else {
    list->head = storage->next;
  }
  if (storage->next != NULL) {
    storage->next->prev = storage->prev;
  } else {
    list->tail = storage->prev;
  }
  list->count--;
  assert_valid_list(list);
}

void grpc_metadata_batch_remove(grpc_exec_ctx *exec_ctx,
                                grpc_metadata_batch *batch,
                                grpc_linked_mdelem *storage) {
  assert_valid_callouts(exec_ctx, batch);
  maybe_unlink_callout(batch, storage);
  unlink_storage(&batch->list, storage);
  GRPC_MDELEM_UNREF(exec_ctx, storage->md);
  assert_valid_callouts(exec_ctx, batch);
}

void grpc_metadata_batch_set_value(grpc_exec_ctx *exec_ctx,
                                   grpc_linked_mdelem *storage,
                                   grpc_slice value) {
  grpc_mdelem old_mdelem = storage->md;
  grpc_mdelem new_mdelem = grpc_mdelem_from_slices(
      exec_ctx, grpc_slice_ref_internal(GRPC_MDKEY(old_mdelem)), value);
  storage->md = new_mdelem;
  GRPC_MDELEM_UNREF(exec_ctx, old_mdelem);
}

grpc_error *grpc_metadata_batch_substitute(grpc_exec_ctx *exec_ctx,
                                           grpc_metadata_batch *batch,
                                           grpc_linked_mdelem *storage,
                                           grpc_mdelem new_mdelem) {
  assert_valid_callouts(exec_ctx, batch);
  grpc_error *error = GRPC_ERROR_NONE;
  grpc_mdelem old_mdelem = storage->md;
  if (!grpc_slice_eq(GRPC_MDKEY(new_mdelem), GRPC_MDKEY(old_mdelem))) {
    maybe_unlink_callout(batch, storage);
    storage->md = new_mdelem;
    error = maybe_link_callout(batch, storage);
    if (error != GRPC_ERROR_NONE) {
      unlink_storage(&batch->list, storage);
      GRPC_MDELEM_UNREF(exec_ctx, storage->md);
    }
  } else {
    storage->md = new_mdelem;
  }
  GRPC_MDELEM_UNREF(exec_ctx, old_mdelem);
  assert_valid_callouts(exec_ctx, batch);
  return error;
}

void grpc_metadata_batch_clear(grpc_exec_ctx *exec_ctx,
                               grpc_metadata_batch *batch) {
  grpc_metadata_batch_destroy(exec_ctx, batch);
  grpc_metadata_batch_init(batch);
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

static void add_error(grpc_error **composite, grpc_error *error,
                      const char *composite_error_string) {
  if (error == GRPC_ERROR_NONE) return;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE_FROM_COPIED_STRING(composite_error_string);
  }
  *composite = grpc_error_add_child(*composite, error);
}

grpc_error *grpc_metadata_batch_filter(grpc_exec_ctx *exec_ctx,
                                       grpc_metadata_batch *batch,
                                       grpc_metadata_batch_filter_func func,
                                       void *user_data,
                                       const char *composite_error_string) {
  grpc_linked_mdelem *l = batch->list.head;
  grpc_error *error = GRPC_ERROR_NONE;
  while (l) {
    grpc_linked_mdelem *next = l->next;
    grpc_filtered_mdelem new_mdelem = func(exec_ctx, user_data, l->md);
    add_error(&error, new_mdelem.error, composite_error_string);
    if (GRPC_MDISNULL(new_mdelem.md)) {
      grpc_metadata_batch_remove(exec_ctx, batch, l);
    } else if (new_mdelem.md.payload != l->md.payload) {
      grpc_metadata_batch_substitute(exec_ctx, batch, l, new_mdelem.md);
    }
    l = next;
  }
  return error;
}
