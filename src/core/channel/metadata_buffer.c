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

#include "src/core/channel/metadata_buffer.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include <string.h>

#define INITIAL_ELEM_CAP 8

/* One queued call; we track offsets to string data in a shared buffer to
   reduce allocations. See grpc_metadata_buffer_impl for the memory use
   strategy */
typedef struct {
  grpc_mdelem *md;
  void (*cb)(void *user_data, grpc_op_error error);
  void *user_data;
  gpr_uint32 flags;
} qelem;

/* Memory layout:

  grpc_metadata_buffer_impl
  followed by an array of qelem  */
struct grpc_metadata_buffer_impl {
  /* number of elements in q */
  size_t elems;
  /* capacity of q */
  size_t elem_cap;
};

#define ELEMS(buffer) ((qelem *)((buffer) + 1))

void grpc_metadata_buffer_init(grpc_metadata_buffer *buffer) {
  /* start buffer as NULL, indicating no elements */
  *buffer = NULL;
}

void grpc_metadata_buffer_destroy(grpc_metadata_buffer *buffer,
                                  grpc_op_error error) {
  size_t i;
  qelem *qe;
  if (*buffer) {
    for (i = 0; i < (*buffer)->elems; i++) {
      qe = &ELEMS(*buffer)[i];
      grpc_mdelem_unref(qe->md);
      qe->cb(qe->user_data, error);
    }
    gpr_free(*buffer);
  }
}

void grpc_metadata_buffer_queue(grpc_metadata_buffer *buffer,
                                grpc_call_op *op) {
  grpc_metadata_buffer_impl *impl = *buffer;
  qelem *qe;
  size_t bytes;

  GPR_ASSERT(op->type == GRPC_SEND_METADATA || op->type == GRPC_RECV_METADATA);

  if (!impl) {
    /* this is the first element: allocate enough space to hold the
       header object and the initial element capacity of qelems */
    bytes =
        sizeof(grpc_metadata_buffer_impl) + INITIAL_ELEM_CAP * sizeof(qelem);
    impl = gpr_malloc(bytes);
    /* initialize the header object */
    impl->elems = 0;
    impl->elem_cap = INITIAL_ELEM_CAP;
  } else if (impl->elems == impl->elem_cap) {
    /* more qelems than what we can deal with: grow by doubling size */
    impl->elem_cap *= 2;
    bytes = sizeof(grpc_metadata_buffer_impl) + impl->elem_cap * sizeof(qelem);
    impl = gpr_realloc(impl, bytes);
  }

  /* append an element to the queue */
  qe = &ELEMS(impl)[impl->elems];
  impl->elems++;

  qe->md = op->data.metadata;
  qe->cb = op->done_cb;
  qe->user_data = op->user_data;
  qe->flags = op->flags;

  /* header object may have changed location: store it back */
  *buffer = impl;
}

void grpc_metadata_buffer_flush(grpc_metadata_buffer *buffer,
                                grpc_call_element *elem) {
  grpc_metadata_buffer_impl *impl = *buffer;
  grpc_call_op op;
  qelem *qe;
  size_t i;

  if (!impl) {
    /* nothing to send */
    return;
  }

  /* construct call_op's, and push them down the stack */
  op.type = GRPC_SEND_METADATA;
  op.dir = GRPC_CALL_DOWN;
  for (i = 0; i < impl->elems; i++) {
    qe = &ELEMS(impl)[i];
    op.done_cb = qe->cb;
    op.user_data = qe->user_data;
    op.flags = qe->flags;
    op.data.metadata = qe->md;
    grpc_call_next_op(elem, &op);
  }

  /* free data structures and reset to NULL: we can only flush once */
  gpr_free(impl);
  *buffer = NULL;
}
