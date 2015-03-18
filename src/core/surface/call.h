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

#ifndef GRPC_INTERNAL_CORE_SURFACE_CALL_H
#define GRPC_INTERNAL_CORE_SURFACE_CALL_H

#include "src/core/channel/channel_stack.h"
#include "src/core/channel/metadata_buffer.h"
#include <grpc/grpc.h>

/* Primitive operation types - grpc_op's get rewritten into these */
typedef enum {
  GRPC_IOREQ_RECV_INITIAL_METADATA,
  GRPC_IOREQ_RECV_MESSAGE,
  GRPC_IOREQ_RECV_TRAILING_METADATA,
  GRPC_IOREQ_RECV_STATUS,
  GRPC_IOREQ_RECV_STATUS_DETAILS,
  GRPC_IOREQ_RECV_CLOSE,
  GRPC_IOREQ_SEND_INITIAL_METADATA,
  GRPC_IOREQ_SEND_MESSAGE,
  GRPC_IOREQ_SEND_TRAILING_METADATA,
  GRPC_IOREQ_SEND_STATUS,
  GRPC_IOREQ_SEND_CLOSE,
  GRPC_IOREQ_OP_COUNT
} grpc_ioreq_op;

typedef union {
  grpc_metadata_array *recv_metadata;
  grpc_byte_buffer **recv_message;
  struct {
    void (*set_value)(grpc_status_code status, void *user_data);
    void *user_data;
  } recv_status;
  struct {
    char **details;
    size_t *details_capacity;
  } recv_status_details;
  struct {
    size_t count;
    const grpc_metadata *metadata;
  } send_metadata;
  grpc_byte_buffer *send_message;
  struct {
    grpc_status_code code;
    const char *details;
  } send_status;
} grpc_ioreq_data;

typedef struct {
  grpc_ioreq_op op;
  grpc_ioreq_data data;
} grpc_ioreq;

typedef void (*grpc_ioreq_completion_func)(grpc_call *call,
                                           grpc_op_error status,
                                           void *user_data);

grpc_call *grpc_call_create(grpc_channel *channel, grpc_completion_queue *cq,
                            const void *server_transport_data);

void grpc_call_set_completion_queue(grpc_call *call, grpc_completion_queue *cq);
grpc_completion_queue *grpc_call_get_completion_queue(grpc_call *call);

void grpc_call_internal_ref(grpc_call *call);
void grpc_call_internal_unref(grpc_call *call, int allow_immediate_deletion);

/* Helpers for grpc_client, grpc_server filters to publish received data to
   the completion queue/surface layer */
void grpc_call_recv_metadata(grpc_call_element *surface_element,
                             grpc_mdelem *md);
void grpc_call_recv_message(grpc_call_element *surface_element,
                            grpc_byte_buffer *message);
void grpc_call_read_closed(grpc_call_element *surface_element);
void grpc_call_stream_closed(grpc_call_element *surface_element);

void grpc_call_execute_op(grpc_call *call, grpc_call_op *op);
grpc_call_error grpc_call_start_ioreq_and_call_back(
    grpc_call *call, const grpc_ioreq *reqs, size_t nreqs,
    grpc_ioreq_completion_func on_complete, void *user_data);

/* Called when it's known that the initial batch of metadata is complete */
void grpc_call_initial_metadata_complete(grpc_call_element *surface_element);

void grpc_call_set_deadline(grpc_call_element *surface_element,
                            gpr_timespec deadline);

grpc_call_stack *grpc_call_get_call_stack(grpc_call *call);

/* Given the top call_element, get the call object. */
grpc_call *grpc_call_from_top_element(grpc_call_element *surface_element);

#endif  /* GRPC_INTERNAL_CORE_SURFACE_CALL_H */
