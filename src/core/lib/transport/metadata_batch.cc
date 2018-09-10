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

#include "src/core/lib/transport/metadata_batch.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

static_hpack_table_metadata_info static_hpack_table_metadata[] = {
    {0, 0, GRPC_BATCH_CALLOUTS_COUNT},  // NOT USED
    {GRPC_MDELEM_AUTHORITY_EMPTY_INDEX, 10 + 32, GRPC_BATCH_AUTHORITY},
    {GRPC_MDELEM_METHOD_GET_INDEX, 10 + 32, GRPC_BATCH_METHOD},
    {GRPC_MDELEM_METHOD_POST_INDEX, 11 + 32, GRPC_BATCH_METHOD},
    {GRPC_MDELEM_PATH_SLASH_INDEX, 6 + 32, GRPC_BATCH_PATH},
    {GRPC_MDELEM_PATH_SLASH_INDEX_DOT_HTML_INDEX, 16 + 32, GRPC_BATCH_PATH},
    {GRPC_MDELEM_SCHEME_HTTP_INDEX, 11 + 32, GRPC_BATCH_SCHEME},
    {GRPC_MDELEM_SCHEME_HTTPS_INDEX, 12 + 32, GRPC_BATCH_SCHEME},
    {GRPC_MDELEM_STATUS_200_INDEX, 10 + 32, GRPC_BATCH_STATUS},
    {GRPC_MDELEM_STATUS_204_INDEX, 10 + 32, GRPC_BATCH_STATUS},
    {GRPC_MDELEM_STATUS_206_INDEX, 10 + 32, GRPC_BATCH_STATUS},
    {GRPC_MDELEM_STATUS_304_INDEX, 10 + 32, GRPC_BATCH_STATUS},
    {GRPC_MDELEM_STATUS_400_INDEX, 10 + 32, GRPC_BATCH_STATUS},
    {GRPC_MDELEM_STATUS_404_INDEX, 10 + 32, GRPC_BATCH_STATUS},
    {GRPC_MDELEM_STATUS_500_INDEX, 10 + 32, GRPC_BATCH_STATUS},
    {GRPC_MDELEM_ACCEPT_CHARSET_EMPTY_INDEX, 14 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_ACCEPT_ENCODING_GZIP_DEFLATE_INDEX, 28 + 32,
     GRPC_BATCH_ACCEPT_ENCODING},
    {GRPC_MDELEM_MDELEM_ACCEPT_LANGUAGE_EMPTY_INDEX, 15 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_MDELEM_ACCEPT_RANGES_EMPTY_INDEX, 13 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_ACCEPT_EMPTY_INDEX, 6 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_ACCESS_CONTROL_ALLOW_ORIGIN_EMPTY_INDEX, 27 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_AGE_EMPTY_INDEX, 3 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_ALLOW_EMPTY_INDEX, 5 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_AUTHORIZATION_EMPTY_INDEX, 13 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_CACHE_CONTROL_EMPTY_INDEX, 13 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_CONTENT_DISPOSITION_EMPTY_INDEX, 19 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_CONTENT_ENCODING_EMPTY_INDEX, 16 + 32,
     GRPC_BATCH_CONTENT_ENCODING},
    {GRPC_MDELEM_CONTENT_LANGUAGE_EMPTY_INDEX, 16 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_CONTENT_LENGTH_EMPTY_INDEX, 14 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_CONTENT_LOCATION_EMPTY_INDEX, 16 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_CONTENT_RANGE_EMPTY_INDEX, 13 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_CONTENT_TYPE_EMPTY_INDEX, 12 + 32, GRPC_BATCH_CONTENT_TYPE},
    {GRPC_MDELEM_COOKIE_EMPTY_INDEX, 6 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_DATE_EMPTY_INDEX, 4 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_ETAG_EMPTY_INDEX, 4 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_EXPECT_EMPTY_INDEX, 6 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_EXPIRES_EMPTY_INDEX, 7 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_FROM_EMPTY_INDEX, 4 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_HOST_EMPTY_INDEX, 4 + 32, GRPC_BATCH_HOST},
    {GRPC_MDELEM_IF_MATCH_EMPTY_INDEX, 8 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_IF_MODIFIED_SINCE_EMPTY_INDEX, 17 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_IF_NONE_MATCH_EMPTY_INDEX, 13 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_IF_RANGE_EMPTY_INDEX, 8 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_IF_UNMODIFIED_SINCE_EMPTY_INDEX, 19 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_LAST_MODIFIED_EMPTY_INDEX, 13 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_LINK_EMPTY_INDEX, 4 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_LOCATION_EMPTY_INDEX, 8 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_MAX_FORWARDS_EMPTY_INDEX, 12 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_PROXY_AUTHENTICATE_EMPTY_INDEX, 18 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_PROXY_AUTHORIZATION_EMPTY_INDEX, 19 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_RANGE_EMPTY_INDEX, 5 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_REFERER_EMPTY_INDEX, 7 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_REFRESH_EMPTY_INDEX, 7 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_RETRY_AFTER_EMPTY_INDEX, 11 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_SERVER_EMPTY_INDEX, 6 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_COOKIE_EMPTY_INDEX, 10 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_STRICT_TRANSPORT_SECURITY_EMPTY_INDEX, 25 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_TRANSFER_ENCODING_EMPTY_INDEX, 17 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_USER_AGENT_EMPTY_INDEX, 10 + 32, GRPC_BATCH_USER_AGENT},
    {GRPC_MDELEM_VARY_EMPTY_INDEX, 4 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_VIA_EMPTY_INDEX, 3 + 32, GRPC_BATCH_CALLOUTS_COUNT},
    {GRPC_MDELEM_WWW_AUTHENTICATE_EMPTY_INDEX, 16 + 32,
     GRPC_BATCH_CALLOUTS_COUNT},
};

/* This is a faster check for seeing if a mdelem index is used or not. To verify
   that the index value is valid, use
   'grpc_metadata_batch_is_valid_mdelem_index' */
static bool is_mdelem_index_used(uint8_t index);

static void set_mdelem_index_unused(uint8_t* index);

static grpc_metadata_batch_callouts_index get_callouts_index(
    grpc_linked_mdelem* storage);

static void assert_valid_list(grpc_mdelem_list* list) {
#ifndef NDEBUG
  grpc_linked_mdelem* l;

  GPR_ASSERT((list->head == nullptr) == (list->tail == nullptr));
  if (!list->head) return;
  GPR_ASSERT(list->head->prev == nullptr);
  GPR_ASSERT(list->tail->next == nullptr);
  GPR_ASSERT((list->head == list->tail) == (list->head->next == nullptr));

  size_t verified_count = 0;
  for (l = list->head; l; l = l->next) {
    GPR_ASSERT(is_mdelem_index_used(l->md_index) || !GRPC_MDISNULL(l->md));
    GPR_ASSERT((l->prev == nullptr) == (l == list->head));
    GPR_ASSERT((l->next == nullptr) == (l == list->tail));
    if (l->next) GPR_ASSERT(l->next->prev == l);
    if (l->prev) GPR_ASSERT(l->prev->next == l);
    verified_count++;
  }
  GPR_ASSERT(list->count == verified_count);
#endif /* NDEBUG */
}

static void assert_valid_callouts(grpc_metadata_batch* batch) {
#ifndef NDEBUG
  for (grpc_linked_mdelem* l = batch->list.head; l != nullptr; l = l->next) {
    grpc_metadata_batch_callouts_index callout_idx;
    if (is_mdelem_index_used(l->md_index)) {
      GPR_ASSERT(grpc_metadata_batch_is_valid_mdelem_index(l->md_index));
      callout_idx = get_callouts_index(l);
      if (callout_idx != GRPC_BATCH_CALLOUTS_COUNT) {
        GPR_ASSERT(batch->idx.array[callout_idx] == l);
      }
    } else {
      grpc_slice key_interned = grpc_slice_intern(GRPC_MDKEY(l->md));
      callout_idx = GRPC_BATCH_INDEX_OF(key_interned);
      if (callout_idx != GRPC_BATCH_CALLOUTS_COUNT) {
        GPR_ASSERT(batch->idx.array[callout_idx] == l);
      }
      grpc_slice_unref_internal(key_interned);
    }
  }
#endif
}

#ifndef NDEBUG
void grpc_metadata_batch_assert_ok(grpc_metadata_batch* batch) {
  assert_valid_list(&batch->list);
}
#endif /* NDEBUG */

void grpc_metadata_batch_init(grpc_metadata_batch* batch) {
  memset(batch, 0, sizeof(*batch));
  batch->deadline = GRPC_MILLIS_INF_FUTURE;
}

void grpc_metadata_batch_destroy(grpc_metadata_batch* batch) {
  grpc_linked_mdelem* l;
  for (l = batch->list.head; l; l = l->next) {
    if (!is_mdelem_index_used(l->md_index)) {
      GRPC_MDELEM_UNREF(l->md);
    }
  }
}

grpc_error* grpc_attach_md_to_error(grpc_error* src, grpc_mdelem md) {
  grpc_error* out = grpc_error_set_str(
      grpc_error_set_str(src, GRPC_ERROR_STR_KEY,
                         grpc_slice_ref_internal(GRPC_MDKEY(md))),
      GRPC_ERROR_STR_VALUE, grpc_slice_ref_internal(GRPC_MDVALUE(md)));
  return out;
}

static grpc_metadata_batch_callouts_index get_callouts_index(
    grpc_linked_mdelem* storage) {
  if (is_mdelem_index_used(storage->md_index)) {
    return static_hpack_table_metadata[storage->md_index].callouts_index;
  } else {
    return GRPC_BATCH_INDEX_OF(GRPC_MDKEY(storage->md));
  }
}

static grpc_error* maybe_link_callout(grpc_metadata_batch* batch,
                                      grpc_linked_mdelem* storage)
    GRPC_MUST_USE_RESULT;

static grpc_error* maybe_link_callout(grpc_metadata_batch* batch,
                                      grpc_linked_mdelem* storage) {
  grpc_metadata_batch_callouts_index idx = get_callouts_index(storage);
  if (idx == GRPC_BATCH_CALLOUTS_COUNT) {
    return GRPC_ERROR_NONE;
  }
  if (batch->idx.array[idx] == nullptr) {
    if (grpc_static_callout_is_default[idx]) ++batch->list.default_count;
    batch->idx.array[idx] = storage;
    return GRPC_ERROR_NONE;
  }
  grpc_error* err;
  if (is_mdelem_index_used(storage->md_index)) {
    char* message;
    gpr_asprintf(&message,
                 "Unallowed duplicate metadata with static hpack table index "
                 "%d and callouts index %d",
                 storage->md_index, idx);
    err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(message);
    gpr_free(message);
  } else {
    err = grpc_attach_md_to_error(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unallowed duplicate metadata"),
        storage->md);
  }
  return err;
}

static void maybe_unlink_callout(grpc_metadata_batch* batch,
                                 grpc_linked_mdelem* storage) {
  grpc_metadata_batch_callouts_index idx = get_callouts_index(storage);
  if (idx == GRPC_BATCH_CALLOUTS_COUNT) {
    return;
  }
  if (grpc_static_callout_is_default[idx]) --batch->list.default_count;
  GPR_ASSERT(batch->idx.array[idx] != nullptr);
  batch->idx.array[idx] = nullptr;
}

bool grpc_metadata_batch_is_valid_mdelem_index(uint8_t index) {
  return index >= MIN_STATIC_HPACK_TABLE_IDX &&
         index <= MAX_STATIC_HPACK_TABLE_IDX;
}

static bool is_mdelem_index_used(uint8_t index) { return index != 0; }

static void set_mdelem_index_unused(uint8_t* index) {
  GPR_ASSERT(index != nullptr);
  *index = 0;
}

grpc_error* grpc_metadata_batch_add_head(grpc_metadata_batch* batch,
                                         grpc_linked_mdelem* storage,
                                         grpc_mdelem elem_to_add) {
  GPR_ASSERT(!GRPC_MDISNULL(elem_to_add));
  storage->md_index = 0;
  storage->md = elem_to_add;
  return grpc_metadata_batch_link_head(batch, storage);
}

grpc_error* grpc_metadata_batch_add_head_static(grpc_metadata_batch* batch,
                                                grpc_linked_mdelem* storage,
                                                uint8_t index_to_add) {
  GPR_ASSERT(grpc_metadata_batch_is_valid_mdelem_index(index_to_add));
  storage->md_index = index_to_add;
  return grpc_metadata_batch_link_head(batch, storage);
}

static void link_head(grpc_mdelem_list* list, grpc_linked_mdelem* storage) {
  assert_valid_list(list);
  GPR_ASSERT(grpc_metadata_batch_is_valid_mdelem_index(storage->md_index) ||
             !GRPC_MDISNULL(storage->md));
  storage->prev = nullptr;
  storage->next = list->head;
  if (list->head != nullptr) {
    list->head->prev = storage;
  } else {
    list->tail = storage;
  }
  list->head = storage;
  list->count++;
  assert_valid_list(list);
}

grpc_error* grpc_metadata_batch_link_head(grpc_metadata_batch* batch,
                                          grpc_linked_mdelem* storage) {
  assert_valid_callouts(batch);
  grpc_error* err = maybe_link_callout(batch, storage);
  if (err != GRPC_ERROR_NONE) {
    assert_valid_callouts(batch);
    return err;
  }
  link_head(&batch->list, storage);
  assert_valid_callouts(batch);
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_metadata_batch_add_tail(grpc_metadata_batch* batch,
                                         grpc_linked_mdelem* storage,
                                         grpc_mdelem elem_to_add) {
  GPR_ASSERT(!GRPC_MDISNULL(elem_to_add));
  storage->md = elem_to_add;
  storage->md_index = 0;
  return grpc_metadata_batch_link_tail(batch, storage);
}

grpc_error* grpc_metadata_batch_add_tail_static(grpc_metadata_batch* batch,
                                                grpc_linked_mdelem* storage,
                                                uint8_t index_to_add) {
  GPR_ASSERT(grpc_metadata_batch_is_valid_mdelem_index(index_to_add));
  storage->md_index = index_to_add;
  return grpc_metadata_batch_link_tail(batch, storage);
}

static void link_tail(grpc_mdelem_list* list, grpc_linked_mdelem* storage) {
  assert_valid_list(list);
  GPR_ASSERT(grpc_metadata_batch_is_valid_mdelem_index(storage->md_index) ||
             !GRPC_MDISNULL(storage->md));
  storage->prev = list->tail;
  storage->next = nullptr;
  storage->reserved = nullptr;
  if (list->tail != nullptr) {
    list->tail->next = storage;
  } else {
    list->head = storage;
  }
  list->tail = storage;
  list->count++;
  assert_valid_list(list);
}

grpc_error* grpc_metadata_batch_link_tail(grpc_metadata_batch* batch,
                                          grpc_linked_mdelem* storage) {
  assert_valid_callouts(batch);
  grpc_error* err = maybe_link_callout(batch, storage);
  if (err != GRPC_ERROR_NONE) {
    assert_valid_callouts(batch);
    return err;
  }
  link_tail(&batch->list, storage);
  assert_valid_callouts(batch);
  return GRPC_ERROR_NONE;
}

static void unlink_storage(grpc_mdelem_list* list,
                           grpc_linked_mdelem* storage) {
  assert_valid_list(list);
  if (storage->prev != nullptr) {
    storage->prev->next = storage->next;
  } else {
    list->head = storage->next;
  }
  if (storage->next != nullptr) {
    storage->next->prev = storage->prev;
  } else {
    list->tail = storage->prev;
  }
  list->count--;
  assert_valid_list(list);
}

void grpc_metadata_batch_remove(grpc_metadata_batch* batch,
                                grpc_linked_mdelem* storage) {
  assert_valid_callouts(batch);
  maybe_unlink_callout(batch, storage);
  unlink_storage(&batch->list, storage);
  if (!is_mdelem_index_used(storage->md_index)) {
    GRPC_MDELEM_UNREF(storage->md);
  }
  assert_valid_callouts(batch);
}

void grpc_metadata_batch_set_value(grpc_linked_mdelem* storage,
                                   grpc_slice value) {
  set_mdelem_index_unused(&storage->md_index);
  grpc_mdelem old_mdelem = storage->md;
  grpc_mdelem new_mdelem = grpc_mdelem_from_slices(
      grpc_slice_ref_internal(GRPC_MDKEY(old_mdelem)), value);
  storage->md = new_mdelem;
  GRPC_MDELEM_UNREF(old_mdelem);
}

grpc_error* grpc_metadata_batch_substitute(grpc_metadata_batch* batch,
                                           grpc_linked_mdelem* storage,
                                           grpc_mdelem new_mdelem) {
  assert_valid_callouts(batch);
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_mdelem old_mdelem = storage->md;
  bool is_index_used = is_mdelem_index_used(storage->md_index);
  if (is_index_used ||
      !grpc_slice_eq(GRPC_MDKEY(new_mdelem), GRPC_MDKEY(old_mdelem))) {
    maybe_unlink_callout(batch, storage);
    storage->md = new_mdelem;
    set_mdelem_index_unused(&storage->md_index);
    error = maybe_link_callout(batch, storage);
    if (error != GRPC_ERROR_NONE) {
      unlink_storage(&batch->list, storage);
      if (!is_index_used) {
        GRPC_MDELEM_UNREF(storage->md);
      }
    }
  } else {
    storage->md = new_mdelem;
  }

  if (!is_index_used) {
    GRPC_MDELEM_UNREF(old_mdelem);
  }
  assert_valid_callouts(batch);
  return error;
}

void grpc_metadata_batch_clear(grpc_metadata_batch* batch) {
  grpc_metadata_batch_destroy(batch);
  grpc_metadata_batch_init(batch);
}

bool grpc_metadata_batch_is_empty(grpc_metadata_batch* batch) {
  return batch->list.head == nullptr &&
         batch->deadline == GRPC_MILLIS_INF_FUTURE;
}

size_t grpc_metadata_batch_size(grpc_metadata_batch* batch) {
  size_t size = 0;
  for (grpc_linked_mdelem* elem = batch->list.head; elem != nullptr;
       elem = elem->next) {
    if (!is_mdelem_index_used(elem->md_index)) {
      size += GRPC_MDELEM_LENGTH(elem->md);
    } else {  // Mdelem is represented by static hpack table index
      size += static_hpack_table_metadata[elem->md_index].size;
    }
  }
  return size;
}

static void add_error(grpc_error** composite, grpc_error* error,
                      const char* composite_error_string) {
  if (error == GRPC_ERROR_NONE) return;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE_FROM_COPIED_STRING(composite_error_string);
  }
  *composite = grpc_error_add_child(*composite, error);
}

grpc_error* grpc_metadata_batch_filter(grpc_metadata_batch* batch,
                                       grpc_metadata_batch_filter_func func,
                                       void* user_data,
                                       const char* composite_error_string) {
  grpc_linked_mdelem* l = batch->list.head;
  grpc_error* error = GRPC_ERROR_NONE;
  while (l) {
    grpc_linked_mdelem* next = l->next;
    // TODO(hcaseyal): provide a mechanism to filter mdelems with indices
    if (!is_mdelem_index_used(l->md_index)) {
      grpc_filtered_mdelem new_mdelem = func(user_data, l->md);
      add_error(&error, new_mdelem.error, composite_error_string);
      if (GRPC_MDISNULL(new_mdelem.md)) {
        grpc_metadata_batch_remove(batch, l);
      } else if (new_mdelem.md.payload != l->md.payload) {
        grpc_metadata_batch_substitute(batch, l, new_mdelem.md);
      }
    }
    l = next;
  }
  return error;
}

void grpc_metadata_batch_copy(grpc_metadata_batch* src,
                              grpc_metadata_batch* dst,
                              grpc_linked_mdelem* storage) {
  grpc_metadata_batch_init(dst);
  dst->deadline = src->deadline;
  size_t i = 0;
  for (grpc_linked_mdelem* elem = src->list.head; elem != nullptr;
       elem = elem->next) {
    grpc_error* error = nullptr;
    if (is_mdelem_index_used(elem->md_index)) {
      error = grpc_metadata_batch_add_tail_static(dst, &storage[i++],
                                                  elem->md_index);
    } else {
      error = grpc_metadata_batch_add_tail(dst, &storage[i++],
                                           GRPC_MDELEM_REF(elem->md));
    }
    // The only way that grpc_metadata_batch_add_tail() can fail is if
    // there's a duplicate entry for a callout.  However, that can't be
    // the case here, because we would not have been allowed to create
    // a source batch that had that kind of conflict.
    GPR_ASSERT(error == GRPC_ERROR_NONE);
  }
}

void grpc_metadata_batch_move(grpc_metadata_batch* src,
                              grpc_metadata_batch* dst) {
  *dst = *src;
  grpc_metadata_batch_init(src);
}
