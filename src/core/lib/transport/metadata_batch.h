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

#ifndef GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H
#define GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H

#include <grpc/support/port_platform.h>

#include <stdbool.h>

#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/static_metadata.h"

typedef struct grpc_linked_mdelem {
  grpc_linked_mdelem() {}

  grpc_mdelem md;
  struct grpc_linked_mdelem* next = nullptr;
  struct grpc_linked_mdelem* prev = nullptr;
  void* reserved;
} grpc_linked_mdelem;

typedef struct grpc_mdelem_list {
  size_t count;
  size_t default_count;  // Number of default keys.
  grpc_linked_mdelem* head;
  grpc_linked_mdelem* tail;
} grpc_mdelem_list;

typedef struct grpc_metadata_batch {
  /** Metadata elements in this batch */
  grpc_mdelem_list list;
  grpc_metadata_batch_callouts idx;
  /** Used to calculate grpc-timeout at the point of sending,
      or GRPC_MILLIS_INF_FUTURE if this batch does not need to send a
      grpc-timeout */
  grpc_millis deadline;

  template <typename Encoder>
  void Encode(Encoder* encoder) const {
    for (auto* l = list.head; l; l = l->next) {
      encoder->Encode(l->md);
    }
    if (deadline != GRPC_MILLIS_INF_FUTURE) encoder->EncodeDeadline(deadline);
  }
} grpc_metadata_batch;

void grpc_metadata_batch_init(grpc_metadata_batch* batch);
void grpc_metadata_batch_destroy(grpc_metadata_batch* batch);
void grpc_metadata_batch_clear(grpc_metadata_batch* batch);
bool grpc_metadata_batch_is_empty(grpc_metadata_batch* batch);

/* Returns the transport size of the batch. */
size_t grpc_metadata_batch_size(grpc_metadata_batch* batch);

/** Remove \a storage from the batch, unreffing the mdelem contained */
void grpc_metadata_batch_remove(grpc_metadata_batch* batch,
                                grpc_linked_mdelem* storage);
void grpc_metadata_batch_remove(grpc_metadata_batch* batch,
                                grpc_metadata_batch_callouts_index idx);

/** Substitute a new mdelem for an old value */
grpc_error_handle grpc_metadata_batch_substitute(grpc_metadata_batch* batch,
                                                 grpc_linked_mdelem* storage,
                                                 grpc_mdelem new_mdelem);

void grpc_metadata_batch_set_value(grpc_linked_mdelem* storage,
                                   const grpc_slice& value);

/** Returns metadata value(s) for the specified key.
    If the key is not present in the batch, returns absl::nullopt.
    If the key is present exactly once in the batch, returns a string_view of
    that value.
    If the key is present more than once in the batch, constructs a
    comma-concatenated string of all values in concatenated_value and returns a
    string_view of that string. */
absl::optional<absl::string_view> grpc_metadata_batch_get_value(
    grpc_metadata_batch* batch, absl::string_view target_key,
    std::string* concatenated_value);

/** Add \a storage to the beginning of \a batch. storage->md is
    assumed to be valid.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call. */
grpc_error_handle grpc_metadata_batch_link_head(grpc_metadata_batch* batch,
                                                grpc_linked_mdelem* storage)
    GRPC_MUST_USE_RESULT;
grpc_error_handle grpc_metadata_batch_link_head(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_metadata_batch_callouts_index idx) GRPC_MUST_USE_RESULT;

/** Add \a storage to the end of \a batch. storage->md is
    assumed to be valid.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call. */
grpc_error_handle grpc_metadata_batch_link_tail(grpc_metadata_batch* batch,
                                                grpc_linked_mdelem* storage)
    GRPC_MUST_USE_RESULT;
grpc_error_handle grpc_metadata_batch_link_tail(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_metadata_batch_callouts_index idx) GRPC_MUST_USE_RESULT;

/** Add \a elem_to_add as the first element in \a batch, using
    \a storage as backing storage for the linked list element.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call.
    Takes ownership of \a elem_to_add */
grpc_error_handle grpc_metadata_batch_add_head(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_mdelem elem_to_add) GRPC_MUST_USE_RESULT;

// TODO(arjunroy, roth): Remove redundant methods.
// add/link_head/tail are almost identical.
inline grpc_error_handle GRPC_MUST_USE_RESULT grpc_metadata_batch_add_head(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_metadata_batch_callouts_index idx) {
  return grpc_metadata_batch_link_head(batch, storage, idx);
}

inline grpc_error_handle GRPC_MUST_USE_RESULT grpc_metadata_batch_add_head(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_mdelem elem_to_add, grpc_metadata_batch_callouts_index idx) {
  GPR_DEBUG_ASSERT(!GRPC_MDISNULL(elem_to_add));
  storage->md = elem_to_add;
  return grpc_metadata_batch_add_head(batch, storage, idx);
}

/** Add \a elem_to_add as the last element in \a batch, using
    \a storage as backing storage for the linked list element.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call.
    Takes ownership of \a elem_to_add */
grpc_error_handle grpc_metadata_batch_add_tail(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_mdelem elem_to_add) GRPC_MUST_USE_RESULT;

inline grpc_error_handle GRPC_MUST_USE_RESULT grpc_metadata_batch_add_tail(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_metadata_batch_callouts_index idx) {
  return grpc_metadata_batch_link_tail(batch, storage, idx);
}

inline grpc_error_handle GRPC_MUST_USE_RESULT grpc_metadata_batch_add_tail(
    grpc_metadata_batch* batch, grpc_linked_mdelem* storage,
    grpc_mdelem elem_to_add, grpc_metadata_batch_callouts_index idx) {
  GPR_DEBUG_ASSERT(!GRPC_MDISNULL(elem_to_add));
  storage->md = elem_to_add;
  return grpc_metadata_batch_add_tail(batch, storage, idx);
}

grpc_error_handle grpc_attach_md_to_error(grpc_error_handle src,
                                          grpc_mdelem md);

struct grpc_filtered_mdelem {
  grpc_error_handle error;
  grpc_mdelem md;
};
#define GRPC_FILTERED_ERROR(error) \
  { (error), GRPC_MDNULL }
#define GRPC_FILTERED_MDELEM(md) \
  { GRPC_ERROR_NONE, (md) }
#define GRPC_FILTERED_REMOVE() \
  { GRPC_ERROR_NONE, GRPC_MDNULL }

typedef grpc_filtered_mdelem (*grpc_metadata_batch_filter_func)(
    void* user_data, grpc_mdelem elem);
grpc_error_handle grpc_metadata_batch_filter(
    grpc_metadata_batch* batch, grpc_metadata_batch_filter_func func,
    void* user_data, const char* composite_error_string) GRPC_MUST_USE_RESULT;

#ifndef NDEBUG
void grpc_metadata_batch_assert_ok(grpc_metadata_batch* batch);
#else
#define grpc_metadata_batch_assert_ok(batch) \
  do {                                       \
  } while (0)
#endif

/// Copies \a src to \a dst.  \a storage must point to an array of
/// \a grpc_linked_mdelem structs of at least the same size as \a src.
///
/// For each mdelem in \a src, if the mdelem is of storage types
/// GRPC_MDELEM_STORAGE_INTERNED or GRPC_MDELEM_STORAGE_ALLOCATED,
/// refs the original mdelem for the copy.  Otherwise, makes a new
/// mdelem that will hold its own refs to the key and value slices.
///
/// Currently used only in the retry code.
void grpc_metadata_batch_copy(grpc_metadata_batch* src,
                              grpc_metadata_batch* dst,
                              grpc_linked_mdelem* storage);

void grpc_metadata_batch_move(grpc_metadata_batch* src,
                              grpc_metadata_batch* dst);

#endif /* GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H */
