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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_linked_mdelem {
  grpc_mdelem *md;
  struct grpc_linked_mdelem *next;
  struct grpc_linked_mdelem *prev;
  void *reserved;
} grpc_linked_mdelem;

typedef struct grpc_mdelem_list {
  grpc_linked_mdelem *head;
  grpc_linked_mdelem *tail;
} grpc_mdelem_list;

typedef struct grpc_metadata_batch {
  /** Metadata elements in this batch */
  grpc_mdelem_list list;
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

/** Moves the metadata information from \a src to \a dst. Upon return, \a src is
 * zeroed. */
void grpc_metadata_batch_move(grpc_metadata_batch *dst,
                              grpc_metadata_batch *src);

/** Add \a storage to the beginning of \a batch. storage->md is
    assumed to be valid.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call. */
void grpc_metadata_batch_link_head(grpc_metadata_batch *batch,
                                   grpc_linked_mdelem *storage);
/** Add \a storage to the end of \a batch. storage->md is
    assumed to be valid.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call. */
void grpc_metadata_batch_link_tail(grpc_metadata_batch *batch,
                                   grpc_linked_mdelem *storage);

/** Add \a elem_to_add as the first element in \a batch, using
    \a storage as backing storage for the linked list element.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call.
    Takes ownership of \a elem_to_add */
void grpc_metadata_batch_add_head(grpc_metadata_batch *batch,
                                  grpc_linked_mdelem *storage,
                                  grpc_mdelem *elem_to_add);
/** Add \a elem_to_add as the last element in \a batch, using
    \a storage as backing storage for the linked list element.
    \a storage is owned by the caller and must survive for the
    lifetime of batch. This usually means it should be around
    for the lifetime of the call.
    Takes ownership of \a elem_to_add */
void grpc_metadata_batch_add_tail(grpc_metadata_batch *batch,
                                  grpc_linked_mdelem *storage,
                                  grpc_mdelem *elem_to_add);

/** For each element in \a batch, execute \a filter.
    The return value from \a filter will be substituted for the
    grpc_mdelem passed to \a filter. If \a filter returns NULL,
    the element will be moved to the garbage list. */
void grpc_metadata_batch_filter(grpc_exec_ctx *exec_ctx,
                                grpc_metadata_batch *batch,
                                grpc_mdelem *(*filter)(grpc_exec_ctx *exec_ctx,
                                                       void *user_data,
                                                       grpc_mdelem *elem),
                                void *user_data);

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
