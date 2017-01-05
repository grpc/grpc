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

#include "src/core/lib/transport/transport.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/transport_impl.h"

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_stream_ref(grpc_stream_refcount *refcount, const char *reason) {
  gpr_atm val = gpr_atm_no_barrier_load(&refcount->refs.count);
  gpr_log(GPR_DEBUG, "%s %p:%p   REF %" PRIdPTR "->%" PRIdPTR " %s",
          refcount->object_type, refcount, refcount->destroy.cb_arg, val,
          val + 1, reason);
#else
void grpc_stream_ref(grpc_stream_refcount *refcount) {
#endif
  gpr_ref_non_zero(&refcount->refs);
}

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_stream_unref(grpc_exec_ctx *exec_ctx, grpc_stream_refcount *refcount,
                       const char *reason) {
  gpr_atm val = gpr_atm_no_barrier_load(&refcount->refs.count);
  gpr_log(GPR_DEBUG, "%s %p:%p UNREF %" PRIdPTR "->%" PRIdPTR " %s",
          refcount->object_type, refcount, refcount->destroy.cb_arg, val,
          val - 1, reason);
#else
void grpc_stream_unref(grpc_exec_ctx *exec_ctx,
                       grpc_stream_refcount *refcount) {
#endif
  if (gpr_unref(&refcount->refs)) {
    grpc_closure_sched(exec_ctx, &refcount->destroy, GRPC_ERROR_NONE);
  }
}

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_stream_ref_init(grpc_stream_refcount *refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void *cb_arg,
                          const char *object_type) {
  refcount->object_type = object_type;
#else
void grpc_stream_ref_init(grpc_stream_refcount *refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void *cb_arg) {
#endif
  gpr_ref_init(&refcount->refs, initial_refs);
  grpc_closure_init(&refcount->destroy, cb, cb_arg, grpc_schedule_on_exec_ctx);
}

static void move64(uint64_t *from, uint64_t *to) {
  *to += *from;
  *from = 0;
}

void grpc_transport_move_one_way_stats(grpc_transport_one_way_stats *from,
                                       grpc_transport_one_way_stats *to) {
  move64(&from->framing_bytes, &to->framing_bytes);
  move64(&from->data_bytes, &to->data_bytes);
  move64(&from->header_bytes, &to->header_bytes);
}

void grpc_transport_move_stats(grpc_transport_stream_stats *from,
                               grpc_transport_stream_stats *to) {
  grpc_transport_move_one_way_stats(&from->incoming, &to->incoming);
  grpc_transport_move_one_way_stats(&from->outgoing, &to->outgoing);
}

size_t grpc_transport_stream_size(grpc_transport *transport) {
  return transport->vtable->sizeof_stream;
}

void grpc_transport_destroy(grpc_exec_ctx *exec_ctx,
                            grpc_transport *transport) {
  transport->vtable->destroy(exec_ctx, transport);
}

int grpc_transport_init_stream(grpc_exec_ctx *exec_ctx,
                               grpc_transport *transport, grpc_stream *stream,
                               grpc_stream_refcount *refcount,
                               const void *server_data) {
  return transport->vtable->init_stream(exec_ctx, transport, stream, refcount,
                                        server_data);
}

void grpc_transport_perform_stream_op(grpc_exec_ctx *exec_ctx,
                                      grpc_transport *transport,
                                      grpc_stream *stream,
                                      grpc_transport_stream_op *op) {
  transport->vtable->perform_stream_op(exec_ctx, transport, stream, op);
}

void grpc_transport_perform_op(grpc_exec_ctx *exec_ctx,
                               grpc_transport *transport,
                               grpc_transport_op *op) {
  transport->vtable->perform_op(exec_ctx, transport, op);
}

void grpc_transport_set_pops(grpc_exec_ctx *exec_ctx, grpc_transport *transport,
                             grpc_stream *stream,
                             grpc_polling_entity *pollent) {
  grpc_pollset *pollset;
  grpc_pollset_set *pollset_set;
  if ((pollset = grpc_polling_entity_pollset(pollent)) != NULL) {
    transport->vtable->set_pollset(exec_ctx, transport, stream, pollset);
  } else if ((pollset_set = grpc_polling_entity_pollset_set(pollent)) != NULL) {
    transport->vtable->set_pollset_set(exec_ctx, transport, stream,
                                       pollset_set);
  } else {
    abort();
  }
}

void grpc_transport_destroy_stream(grpc_exec_ctx *exec_ctx,
                                   grpc_transport *transport,
                                   grpc_stream *stream, void *and_free_memory) {
  transport->vtable->destroy_stream(exec_ctx, transport, stream,
                                    and_free_memory);
}

char *grpc_transport_get_peer(grpc_exec_ctx *exec_ctx,
                              grpc_transport *transport) {
  return transport->vtable->get_peer(exec_ctx, transport);
}

grpc_endpoint *grpc_transport_get_endpoint(grpc_exec_ctx *exec_ctx,
                                           grpc_transport *transport) {
  return transport->vtable->get_endpoint(exec_ctx, transport);
}

void grpc_transport_stream_op_finish_with_failure(grpc_exec_ctx *exec_ctx,
                                                  grpc_transport_stream_op *op,
                                                  grpc_error *error) {
  grpc_closure_sched(exec_ctx, op->recv_message_ready, GRPC_ERROR_REF(error));
  grpc_closure_sched(exec_ctx, op->recv_initial_metadata_ready,
                     GRPC_ERROR_REF(error));
  grpc_closure_sched(exec_ctx, op->on_complete, error);
}

typedef struct {
  grpc_error *error;
  grpc_closure *then_call;
  grpc_closure closure;
} close_message_data;

static void free_message(grpc_exec_ctx *exec_ctx, void *p, grpc_error *error) {
  close_message_data *cmd = p;
  GRPC_ERROR_UNREF(cmd->error);
  if (cmd->then_call != NULL) {
    cmd->then_call->cb(exec_ctx, cmd->then_call->cb_arg, error);
  }
  gpr_free(cmd);
}

static void add_error(grpc_transport_stream_op *op, grpc_error **which,
                      grpc_error *error) {
  close_message_data *cmd;
  cmd = gpr_malloc(sizeof(*cmd));
  cmd->error = error;
  cmd->then_call = op->on_complete;
  grpc_closure_init(&cmd->closure, free_message, cmd,
                    grpc_schedule_on_exec_ctx);
  op->on_complete = &cmd->closure;
  *which = error;
}

void grpc_transport_stream_op_add_cancellation(grpc_transport_stream_op *op,
                                               grpc_status_code status) {
  GPR_ASSERT(status != GRPC_STATUS_OK);
  if (op->cancel_error == GRPC_ERROR_NONE) {
    op->cancel_error = grpc_error_set_int(GRPC_ERROR_CANCELLED,
                                          GRPC_ERROR_INT_GRPC_STATUS, status);
    op->close_error = GRPC_ERROR_NONE;
  }
}

void grpc_transport_stream_op_add_cancellation_with_message(
    grpc_exec_ctx *exec_ctx, grpc_transport_stream_op *op,
    grpc_status_code status, grpc_slice *optional_message) {
  GPR_ASSERT(status != GRPC_STATUS_OK);
  if (op->cancel_error != GRPC_ERROR_NONE) {
    if (optional_message) {
      grpc_slice_unref_internal(exec_ctx, *optional_message);
    }
    return;
  }
  grpc_error *error;
  if (optional_message != NULL) {
    char *msg = grpc_dump_slice(*optional_message, GPR_DUMP_ASCII);
    error = grpc_error_set_str(GRPC_ERROR_CREATE(msg),
                               GRPC_ERROR_STR_GRPC_MESSAGE, msg);
    gpr_free(msg);
    grpc_slice_unref_internal(exec_ctx, *optional_message);
  } else {
    error = GRPC_ERROR_CREATE("Call cancelled");
  }
  error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS, status);
  add_error(op, &op->cancel_error, error);
}

void grpc_transport_stream_op_add_close(grpc_exec_ctx *exec_ctx,
                                        grpc_transport_stream_op *op,
                                        grpc_status_code status,
                                        grpc_slice *optional_message) {
  GPR_ASSERT(status != GRPC_STATUS_OK);
  if (op->cancel_error != GRPC_ERROR_NONE ||
      op->close_error != GRPC_ERROR_NONE) {
    if (optional_message) {
      grpc_slice_unref_internal(exec_ctx, *optional_message);
    }
    return;
  }
  grpc_error *error;
  if (optional_message != NULL) {
    char *msg = grpc_dump_slice(*optional_message, GPR_DUMP_ASCII);
    error = grpc_error_set_str(GRPC_ERROR_CREATE(msg),
                               GRPC_ERROR_STR_GRPC_MESSAGE, msg);
    gpr_free(msg);
    grpc_slice_unref_internal(exec_ctx, *optional_message);
  } else {
    error = GRPC_ERROR_CREATE("Call force closed");
  }
  error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS, status);
  add_error(op, &op->close_error, error);
}

typedef struct {
  grpc_closure outer_on_complete;
  grpc_closure *inner_on_complete;
  grpc_transport_op op;
} made_transport_op;

static void destroy_made_transport_op(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_error *error) {
  made_transport_op *op = arg;
  grpc_closure_sched(exec_ctx, op->inner_on_complete, GRPC_ERROR_REF(error));
  gpr_free(op);
}

grpc_transport_op *grpc_make_transport_op(grpc_closure *on_complete) {
  made_transport_op *op = gpr_malloc(sizeof(*op));
  grpc_closure_init(&op->outer_on_complete, destroy_made_transport_op, op,
                    grpc_schedule_on_exec_ctx);
  op->inner_on_complete = on_complete;
  memset(&op->op, 0, sizeof(op->op));
  op->op.on_consumed = &op->outer_on_complete;
  return &op->op;
}

typedef struct {
  grpc_closure outer_on_complete;
  grpc_closure *inner_on_complete;
  grpc_transport_stream_op op;
} made_transport_stream_op;

static void destroy_made_transport_stream_op(grpc_exec_ctx *exec_ctx, void *arg,
                                             grpc_error *error) {
  made_transport_stream_op *op = arg;
  grpc_closure_sched(exec_ctx, op->inner_on_complete, GRPC_ERROR_REF(error));
  gpr_free(op);
}

grpc_transport_stream_op *grpc_make_transport_stream_op(
    grpc_closure *on_complete) {
  made_transport_stream_op *op = gpr_malloc(sizeof(*op));
  grpc_closure_init(&op->outer_on_complete, destroy_made_transport_stream_op,
                    op, grpc_schedule_on_exec_ctx);
  op->inner_on_complete = on_complete;
  memset(&op->op, 0, sizeof(op->op));
  op->op.on_complete = &op->outer_on_complete;
  return &op->op;
}
