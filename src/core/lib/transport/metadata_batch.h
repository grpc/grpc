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

#ifndef GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H
#define GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/static_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_linked_mdelem {
  grpc_mdelem md;
  struct grpc_linked_mdelem *next;
  struct grpc_linked_mdelem *prev;
  void *reserved;
} grpc_linked_mdelem;

typedef struct grpc_mdelem_list {
  size_t count;
  grpc_linked_mdelem *head;
  grpc_linked_mdelem *tail;
} grpc_mdelem_list;

typedef struct grpc_metadata_batch {
  /** Metadata elements in this batch */
  grpc_mdelem_list list;
  grpc_metadata_batch_callouts idx;
  /** Used to calculate grpc-timeout at the point of sending,
      or gpr_inf_future if this batch does not need to send a
      grpc-timeout */
  gpr_timespec deadline;
} grpc_metadata_batch;

void grpc_metadata_batch_init(grpc_metadata_batch *batch);
void grpc_metadata_batch_destroy(grpc_exec_ctx *exec_ctx,
                                 grpc_metadata_batch *batch);
void grpc_metadata_batch_clear(grpc_exec_ctx *exec_ctx,
                               grpc_metadata_batch *batch);
bool grpc_metadata_batch_is_empty(grpc_metadata_batch *batch);

/* Returns the transport size of the batch. */
size_t grpc_metadata_batch_size(grpc_metadata_batch *batch);

/** Remove \a storage from the batch, unreffing the mdelem contained */
void grpc_metadata_batch_remove(grpc_exec_ctx *exec_ctx,
                                grpc_metadata_batch *batch,
                                grpc_linked_mdelem *storage);

/** Substitute a new mdelem for an old value */
grpc_error *grpc_metadata_batch_substitute(grpc_exec_ctx *exec_ctx,
                                           grpc_metadata_batch *batch,
                                           grpc_linked_mdelem *storage,
                                           grpc_mdelem new_value);

void grpc_metadata_batch_set_value(grpc_exec_ctx *exec_ctx,
                                   grpc_linked_mdelem *storage,
                                   grpc_slice value);

/** Add \a storage to the beginning of \a batch. storage->md is
    assumed to be valid.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call. */
grpc_error *grpc_metadata_batch_link_head(
    grpc_exec_ctx *exec_ctx, grpc_metadata_batch *batch,
    grpc_linked_mdelem *storage) GRPC_MUST_USE_RESULT;
/** Add \a storage to the end of \a batch. storage->md is
    assumed to be valid.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call. */
grpc_error *grpc_metadata_batch_link_tail(
    grpc_exec_ctx *exec_ctx, grpc_metadata_batch *batch,
    grpc_linked_mdelem *storage) GRPC_MUST_USE_RESULT;

/** Add \a elem_to_add as the first element in \a batch, using
    \a storage as backing storage for the linked list element.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call.
    Takes ownership of \a elem_to_add */
grpc_error *grpc_metadata_batch_add_head(
    grpc_exec_ctx *exec_ctx, grpc_metadata_batch *batch,
    grpc_linked_mdelem *storage, grpc_mdelem elem_to_add) GRPC_MUST_USE_RESULT;
/** Add \a elem_to_add as the last element in \a batch, using
    \a storage as backing storage for the linked list element.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call.
    Takes ownership of \a elem_to_add */
grpc_error *grpc_metadata_batch_add_tail(
    grpc_exec_ctx *exec_ctx, grpc_metadata_batch *batch,
    grpc_linked_mdelem *storage, grpc_mdelem elem_to_add) GRPC_MUST_USE_RESULT;

grpc_error *grpc_attach_md_to_error(grpc_error *src, grpc_mdelem md);

typedef struct {
  grpc_error *error;
  grpc_mdelem md;
} grpc_filtered_mdelem;

#define GRPC_FILTERED_ERROR(error) \
  ((grpc_filtered_mdelem){(error), GRPC_MDNULL})
#define GRPC_FILTERED_MDELEM(md) ((grpc_filtered_mdelem){GRPC_ERROR_NONE, (md)})
#define GRPC_FILTERED_REMOVE() \
  ((grpc_filtered_mdelem){GRPC_ERROR_NONE, GRPC_MDNULL})

typedef grpc_filtered_mdelem (*grpc_metadata_batch_filter_func)(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_mdelem elem);
grpc_error *grpc_metadata_batch_filter(
    grpc_exec_ctx *exec_ctx, grpc_metadata_batch *batch,
    grpc_metadata_batch_filter_func func, void *user_data,
    const char *composite_error_string) GRPC_MUST_USE_RESULT;

#ifndef NDEBUG
void grpc_metadata_batch_assert_ok(grpc_metadata_batch *comd);
#else
#define grpc_metadata_batch_assert_ok(comd) \
  do {                                      \
  } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_TRANSPORT_METADATA_BATCH_H */
