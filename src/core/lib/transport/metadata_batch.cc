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

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_join.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

namespace grpc_core {

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
    GPR_ASSERT(!GRPC_MDISNULL(l->md));
    GPR_ASSERT((l->prev == nullptr) == (l == list->head));
    GPR_ASSERT((l->next == nullptr) == (l == list->tail));
    if (l->next) GPR_ASSERT(l->next->prev == l);
    if (l->prev) GPR_ASSERT(l->prev->next == l);
    verified_count++;
  }
  GPR_ASSERT(list->count == verified_count);
#else
  // Avoid unused-parameter warning for debug-only parameter
  (void)list;
#endif /* NDEBUG */
}

void MetadataMap::AssertValidCallouts() {
#ifndef NDEBUG
  for (grpc_linked_mdelem* l = list_.head; l != nullptr; l = l->next) {
    grpc_slice key_interned = grpc_slice_intern(GRPC_MDKEY(l->md));
    grpc_metadata_batch_callouts_index callout_idx =
        GRPC_BATCH_INDEX_OF(key_interned);
    if (callout_idx != GRPC_BATCH_CALLOUTS_COUNT) {
      GPR_ASSERT(idx_.array[callout_idx] == l);
    }
    grpc_slice_unref_internal(key_interned);
  }
#endif
}

#ifndef NDEBUG
void MetadataMap::AssertOk() { assert_valid_list(&list_); }
#endif /* NDEBUG */

MetadataMap::MetadataMap() {
  memset(&list_, 0, sizeof(list_));
  memset(&idx_, 0, sizeof(idx_));
  deadline_ = GRPC_MILLIS_INF_FUTURE;
}

MetadataMap::MetadataMap(MetadataMap&& other) noexcept {
  list_ = other.list_;
  idx_ = other.idx_;
  deadline_ = other.deadline_;
  memset(&other.list_, 0, sizeof(list_));
  memset(&other.idx_, 0, sizeof(idx_));
  other.deadline_ = GRPC_MILLIS_INF_FUTURE;
}

MetadataMap::~MetadataMap() {
  AssertValidCallouts();
  for (auto* l = list_.head; l; l = l->next) {
    GRPC_MDELEM_UNREF(l->md);
  }
}

static grpc_error_handle GPR_ATTRIBUTE_NOINLINE error_with_md(grpc_mdelem md) {
  return grpc_attach_md_to_error(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unallowed duplicate metadata"), md);
}

absl::optional<grpc_slice> MetadataMap::Remove(grpc_slice key) {
  for (auto* l = list_.head; l; l = l->next) {
    if (grpc_slice_eq(GRPC_MDKEY(l->md), key)) {
      auto out = grpc_slice_ref_internal(GRPC_MDVALUE(l->md));
      Remove(l);
      return out;
    }
  }
  return {};
}

grpc_error_handle MetadataMap::LinkCallout(
    grpc_linked_mdelem* storage, grpc_metadata_batch_callouts_index idx) {
  AssertValidCallouts();
  GPR_DEBUG_ASSERT(idx >= 0 && idx < GRPC_BATCH_CALLOUTS_COUNT);
  if (GPR_LIKELY(idx_.array[idx] == nullptr)) {
    ++list_.default_count;
    idx_.array[idx] = storage;
    AssertValidCallouts();
    return GRPC_ERROR_NONE;
  }
  AssertValidCallouts();
  return error_with_md(storage->md);
}

grpc_error_handle MetadataMap::MaybeLinkCallout(grpc_linked_mdelem* storage) {
  grpc_metadata_batch_callouts_index idx =
      GRPC_BATCH_INDEX_OF(GRPC_MDKEY(storage->md));
  if (idx == GRPC_BATCH_CALLOUTS_COUNT) {
    return GRPC_ERROR_NONE;
  }
  return LinkCallout(storage, idx);
}

void MetadataMap::MaybeUnlinkCallout(grpc_linked_mdelem* storage) {
  grpc_metadata_batch_callouts_index idx =
      GRPC_BATCH_INDEX_OF(GRPC_MDKEY(storage->md));
  if (idx == GRPC_BATCH_CALLOUTS_COUNT) {
    return;
  }
  --list_.default_count;
  GPR_DEBUG_ASSERT(idx_.array[idx] != nullptr);
  idx_.array[idx] = nullptr;
}

grpc_error_handle MetadataMap::AddHead(grpc_linked_mdelem* storage,
                                       grpc_mdelem elem_to_add) {
  GPR_DEBUG_ASSERT(!GRPC_MDISNULL(elem_to_add));
  storage->md = elem_to_add;
  return LinkHead(storage);
}

static void link_head(grpc_mdelem_list* list, grpc_linked_mdelem* storage) {
  assert_valid_list(list);
  GPR_DEBUG_ASSERT(!GRPC_MDISNULL(storage->md));
  storage->prev = nullptr;
  storage->next = list->head;
  storage->reserved = nullptr;
  if (list->head != nullptr) {
    list->head->prev = storage;
  } else {
    list->tail = storage;
  }
  list->head = storage;
  list->count++;
  assert_valid_list(list);
}

grpc_error_handle MetadataMap::LinkHead(grpc_linked_mdelem* storage) {
  AssertValidCallouts();
  grpc_error_handle err = MaybeLinkCallout(storage);
  if (err != GRPC_ERROR_NONE) {
    AssertValidCallouts();
    return err;
  }
  link_head(&list_, storage);
  AssertValidCallouts();
  return GRPC_ERROR_NONE;
}

// TODO(arjunroy): Need to revisit this and see what guarantees exist between
// C-core and the internal-metadata subsystem. E.g. can we ensure a particular
// metadata is never added twice, even in the presence of user supplied data?
grpc_error_handle MetadataMap::LinkHead(
    grpc_linked_mdelem* storage, grpc_metadata_batch_callouts_index idx) {
  GPR_DEBUG_ASSERT(GRPC_BATCH_INDEX_OF(GRPC_MDKEY(storage->md)) == idx);
  AssertValidCallouts();
  grpc_error_handle err = LinkCallout(storage, idx);
  if (GPR_UNLIKELY(err != GRPC_ERROR_NONE)) {
    AssertValidCallouts();
    return err;
  }
  link_head(&list_, storage);
  AssertValidCallouts();
  return GRPC_ERROR_NONE;
}

grpc_error_handle MetadataMap::AddTail(grpc_linked_mdelem* storage,
                                       grpc_mdelem elem_to_add) {
  GPR_DEBUG_ASSERT(!GRPC_MDISNULL(elem_to_add));
  storage->md = elem_to_add;
  return LinkTail(storage);
}

static void link_tail(grpc_mdelem_list* list, grpc_linked_mdelem* storage) {
  assert_valid_list(list);
  GPR_DEBUG_ASSERT(!GRPC_MDISNULL(storage->md));
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

grpc_error_handle MetadataMap::LinkTail(grpc_linked_mdelem* storage) {
  AssertValidCallouts();
  grpc_error_handle err = MaybeLinkCallout(storage);
  if (err != GRPC_ERROR_NONE) {
    AssertValidCallouts();
    return err;
  }
  link_tail(&list_, storage);
  AssertValidCallouts();
  return GRPC_ERROR_NONE;
}

grpc_error_handle MetadataMap::LinkTail(
    grpc_linked_mdelem* storage, grpc_metadata_batch_callouts_index idx) {
  GPR_DEBUG_ASSERT(GRPC_BATCH_INDEX_OF(GRPC_MDKEY(storage->md)) == idx);
  AssertValidCallouts();
  grpc_error_handle err = LinkCallout(storage, idx);
  if (GPR_UNLIKELY(err != GRPC_ERROR_NONE)) {
    AssertValidCallouts();
    return err;
  }
  link_tail(&list_, storage);
  AssertValidCallouts();
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

void MetadataMap::Remove(grpc_linked_mdelem* storage) {
  AssertValidCallouts();
  MaybeUnlinkCallout(storage);
  unlink_storage(&list_, storage);
  GRPC_MDELEM_UNREF(storage->md);
  AssertValidCallouts();
}

void MetadataMap::Remove(grpc_metadata_batch_callouts_index idx) {
  AssertValidCallouts();
  if (idx_.array[idx] == nullptr) return;
  --list_.default_count;
  unlink_storage(&list_, idx_.array[idx]);
  GRPC_MDELEM_UNREF(idx_.array[idx]->md);
  idx_.array[idx] = nullptr;
  AssertValidCallouts();
}

absl::optional<absl::string_view> MetadataMap::GetValue(
    absl::string_view target_key, std::string* concatenated_value) {
  // Find all values for the specified key.
  absl::InlinedVector<absl::string_view, 1> values;
  for (grpc_linked_mdelem* md = list_.head; md != nullptr; md = md->next) {
    absl::string_view key = grpc_core::StringViewFromSlice(GRPC_MDKEY(md->md));
    absl::string_view value =
        grpc_core::StringViewFromSlice(GRPC_MDVALUE(md->md));
    if (target_key == key) values.push_back(value);
  }
  // If none found, no match.
  if (values.empty()) return absl::nullopt;
  // If exactly one found, return it as-is.
  if (values.size() == 1) return values.front();
  // If more than one found, concatenate the values, using
  // *concatenated_values as a temporary holding place for the
  // concatenated string.
  *concatenated_value = absl::StrJoin(values, ",");
  return *concatenated_value;
}

grpc_error_handle MetadataMap::Substitute(grpc_linked_mdelem* storage,
                                          grpc_mdelem new_mdelem) {
  AssertValidCallouts();
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_mdelem old_mdelem = storage->md;
  if (!grpc_slice_eq(GRPC_MDKEY(new_mdelem), GRPC_MDKEY(old_mdelem))) {
    MaybeUnlinkCallout(storage);
    storage->md = new_mdelem;
    error = MaybeLinkCallout(storage);
    if (error != GRPC_ERROR_NONE) {
      unlink_storage(&list_, storage);
      GRPC_MDELEM_UNREF(storage->md);
    }
  } else {
    storage->md = new_mdelem;
  }
  GRPC_MDELEM_UNREF(old_mdelem);
  AssertValidCallouts();
  return error;
}

void MetadataMap::Clear() {
  this->~MetadataMap();
  new (this) MetadataMap();
}

size_t MetadataMap::TransportSize() const {
  size_t size = 0;
  for (grpc_linked_mdelem* elem = list_.head; elem != nullptr;
       elem = elem->next) {
    size += GRPC_MDELEM_LENGTH(elem->md);
  }
  return size;
}

bool MetadataMap::ReplaceIfExists(grpc_slice key, grpc_slice value) {
  AssertValidCallouts();
  for (grpc_linked_mdelem* l = list_.head; l != nullptr; l = l->next) {
    if (grpc_slice_eq(GRPC_MDKEY(l->md), key)) {
      auto new_mdelem = grpc_mdelem_from_slices(grpc_slice_ref_internal(key),
                                                grpc_slice_ref_internal(value));
      GRPC_MDELEM_UNREF(l->md);
      l->md = new_mdelem;
      AssertValidCallouts();
      return true;
    }
  }
  AssertValidCallouts();
  return false;
}

}  // namespace grpc_core

void grpc_metadata_batch_set_value(grpc_linked_mdelem* storage,
                                   const grpc_slice& value) {
  grpc_mdelem old_mdelem = storage->md;
  grpc_mdelem new_mdelem = grpc_mdelem_from_slices(
      grpc_slice_ref_internal(GRPC_MDKEY(old_mdelem)), value);
  storage->md = new_mdelem;
  GRPC_MDELEM_UNREF(old_mdelem);
}

void grpc_metadata_batch_copy(grpc_metadata_batch* src,
                              grpc_metadata_batch* dst,
                              grpc_linked_mdelem* storage) {
  grpc_metadata_batch_init(dst);
  (*dst)->SetDeadline((*src)->deadline());
  size_t i = 0;
  (*src)->ForEach([&](grpc_mdelem md) {
    // If the mdelem is not external, take a ref.
    // Otherwise, create a new copy, holding its own refs to the
    // underlying slices.
    if (GRPC_MDELEM_STORAGE(md) != GRPC_MDELEM_STORAGE_EXTERNAL) {
      md = GRPC_MDELEM_REF(md);
    } else {
      md = grpc_mdelem_from_slices(grpc_slice_ref_internal(GRPC_MDKEY(md)),
                                   grpc_slice_ref_internal(GRPC_MDVALUE(md)));
    }
    // Error unused in non-debug builds.
    grpc_error_handle GRPC_UNUSED error =
        grpc_metadata_batch_add_tail(dst, &storage[i++], md);
    // The only way that grpc_metadata_batch_add_tail() can fail is if
    // there's a duplicate entry for a callout.  However, that can't be
    // the case here, because we would not have been allowed to create
    // a source batch that had that kind of conflict.
    GPR_DEBUG_ASSERT(error == GRPC_ERROR_NONE);
  });
}

grpc_error_handle grpc_attach_md_to_error(grpc_error_handle src,
                                          grpc_mdelem md) {
  grpc_error_handle out = grpc_error_set_str(
      grpc_error_set_str(src, GRPC_ERROR_STR_KEY,
                         grpc_slice_ref_internal(GRPC_MDKEY(md))),
      GRPC_ERROR_STR_VALUE, grpc_slice_ref_internal(GRPC_MDVALUE(md)));
  return out;
}
