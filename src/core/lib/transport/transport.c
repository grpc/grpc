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
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include "src/core/lib/transport/transport_impl.h"

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_stream_ref(grpc_stream_refcount *refcount, const char *reason) {
  gpr_atm val = gpr_atm_no_barrier_load(&refcount->refs.count);
  gpr_log(GPR_DEBUG, "%s %p:%p   REF %d->%d %s", refcount->object_type,
          refcount, refcount->destroy.cb_arg, val, val + 1, reason);
#else
void grpc_stream_ref(grpc_stream_refcount *refcount) {
#endif
  gpr_ref_non_zero(&refcount->refs);
}

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_stream_unref(grpc_exec_ctx *exec_ctx, grpc_stream_refcount *refcount,
                       const char *reason) {
  gpr_atm val = gpr_atm_no_barrier_load(&refcount->refs.count);
  gpr_log(GPR_DEBUG, "%s %p:%p UNREF %d->%d %s", refcount->object_type,
          refcount, refcount->destroy.cb_arg, val, val - 1, reason);
#else
void grpc_stream_unref(grpc_exec_ctx *exec_ctx,
                       grpc_stream_refcount *refcount) {
#endif
  if (gpr_unref(&refcount->refs)) {
    grpc_exec_ctx_enqueue(exec_ctx, &refcount->destroy, true, NULL);
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
  grpc_closure_init(&refcount->destroy, cb, cb_arg);
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

void grpc_transport_stream_op_finish_with_failure(
    grpc_exec_ctx *exec_ctx, grpc_transport_stream_op *op) {
  grpc_exec_ctx_enqueue(exec_ctx, op->recv_message_ready, false, NULL);
  grpc_exec_ctx_enqueue(exec_ctx, op->recv_initial_metadata_ready, false, NULL);
  grpc_exec_ctx_enqueue(exec_ctx, op->on_complete, false, NULL);
}

void grpc_transport_stream_op_add_cancellation(grpc_transport_stream_op *op,
                                               grpc_status_code status) {
  GPR_ASSERT(status != GRPC_STATUS_OK);
  if (op->cancel_with_status == GRPC_STATUS_OK) {
    op->cancel_with_status = status;
  }
  if (op->close_with_status != GRPC_STATUS_OK) {
    op->close_with_status = GRPC_STATUS_OK;
    if (op->optional_close_message != NULL) {
      gpr_slice_unref(*op->optional_close_message);
      op->optional_close_message = NULL;
    }
  }
}

typedef struct {
  gpr_slice message;
  grpc_closure *then_call;
  grpc_closure closure;
} close_message_data;

static void free_message(grpc_exec_ctx *exec_ctx, void *p, bool iomgr_success) {
  close_message_data *cmd = p;
  gpr_slice_unref(cmd->message);
  if (cmd->then_call != NULL) {
    cmd->then_call->cb(exec_ctx, cmd->then_call->cb_arg, iomgr_success);
  }
  gpr_free(cmd);
}

void grpc_transport_stream_op_add_close(grpc_transport_stream_op *op,
                                        grpc_status_code status,
                                        gpr_slice *optional_message) {
  close_message_data *cmd;
  GPR_ASSERT(status != GRPC_STATUS_OK);
  if (op->cancel_with_status != GRPC_STATUS_OK ||
      op->close_with_status != GRPC_STATUS_OK) {
    if (optional_message) {
      gpr_slice_unref(*optional_message);
    }
    return;
  }
  if (optional_message) {
    cmd = gpr_malloc(sizeof(*cmd));
    cmd->message = *optional_message;
    cmd->then_call = op->on_complete;
    grpc_closure_init(&cmd->closure, free_message, cmd);
    op->on_complete = &cmd->closure;
    op->optional_close_message = &cmd->message;
  }
  op->close_with_status = status;
}
