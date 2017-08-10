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

#include "src/core/lib/transport/transport.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/transport_impl.h"

#ifndef NDEBUG
grpc_tracer_flag grpc_trace_stream_refcount =
    GRPC_TRACER_INITIALIZER(false, "stream_refcount");
#endif

#ifndef NDEBUG
void grpc_stream_ref(grpc_stream_refcount *refcount, const char *reason) {
  if (GRPC_TRACER_ON(grpc_trace_stream_refcount)) {
    gpr_atm val = gpr_atm_no_barrier_load(&refcount->refs.count);
    gpr_log(GPR_DEBUG, "%s %p:%p   REF %" PRIdPTR "->%" PRIdPTR " %s",
            refcount->object_type, refcount, refcount->destroy.cb_arg, val,
            val + 1, reason);
  }
#else
void grpc_stream_ref(grpc_stream_refcount *refcount) {
#endif
  gpr_ref_non_zero(&refcount->refs);
}

#ifndef NDEBUG
void grpc_stream_unref(grpc_exec_ctx *exec_ctx, grpc_stream_refcount *refcount,
                       const char *reason) {
  if (GRPC_TRACER_ON(grpc_trace_stream_refcount)) {
    gpr_atm val = gpr_atm_no_barrier_load(&refcount->refs.count);
    gpr_log(GPR_DEBUG, "%s %p:%p UNREF %" PRIdPTR "->%" PRIdPTR " %s",
            refcount->object_type, refcount, refcount->destroy.cb_arg, val,
            val - 1, reason);
  }
#else
void grpc_stream_unref(grpc_exec_ctx *exec_ctx,
                       grpc_stream_refcount *refcount) {
#endif
  if (gpr_unref(&refcount->refs)) {
    if (exec_ctx->flags & GRPC_EXEC_CTX_FLAG_THREAD_RESOURCE_LOOP) {
      /* Ick.
         The thread we're running on MAY be owned (indirectly) by a call-stack.
         If that's the case, destroying the call-stack MAY try to destroy the
         thread, which is a tangled mess that we just don't want to ever have to
         cope with.
         Throw this over to the executor (on a core-owned thread) and process it
         there. */
      refcount->destroy.scheduler = grpc_executor_scheduler;
    }
    GRPC_CLOSURE_SCHED(exec_ctx, &refcount->destroy, GRPC_ERROR_NONE);
  }
}

#define STREAM_REF_FROM_SLICE_REF(p)         \
  ((grpc_stream_refcount *)(((uint8_t *)p) - \
                            offsetof(grpc_stream_refcount, slice_refcount)))

static void slice_stream_ref(void *p) {
#ifndef NDEBUG
  grpc_stream_ref(STREAM_REF_FROM_SLICE_REF(p), "slice");
#else
  grpc_stream_ref(STREAM_REF_FROM_SLICE_REF(p));
#endif
}

static void slice_stream_unref(grpc_exec_ctx *exec_ctx, void *p) {
#ifndef NDEBUG
  grpc_stream_unref(exec_ctx, STREAM_REF_FROM_SLICE_REF(p), "slice");
#else
  grpc_stream_unref(exec_ctx, STREAM_REF_FROM_SLICE_REF(p));
#endif
}

grpc_slice grpc_slice_from_stream_owned_buffer(grpc_stream_refcount *refcount,
                                               void *buffer, size_t length) {
  slice_stream_ref(&refcount->slice_refcount);
  return (grpc_slice){.refcount = &refcount->slice_refcount,
                      .data.refcounted = {.bytes = buffer, .length = length}};
}

static const grpc_slice_refcount_vtable stream_ref_slice_vtable = {
    .ref = slice_stream_ref,
    .unref = slice_stream_unref,
    .eq = grpc_slice_default_eq_impl,
    .hash = grpc_slice_default_hash_impl};

#ifndef NDEBUG
void grpc_stream_ref_init(grpc_stream_refcount *refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void *cb_arg,
                          const char *object_type) {
  refcount->object_type = object_type;
#else
void grpc_stream_ref_init(grpc_stream_refcount *refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void *cb_arg) {
#endif
  gpr_ref_init(&refcount->refs, initial_refs);
  GRPC_CLOSURE_INIT(&refcount->destroy, cb, cb_arg, grpc_schedule_on_exec_ctx);
  refcount->slice_refcount.vtable = &stream_ref_slice_vtable;
  refcount->slice_refcount.sub_refcount = &refcount->slice_refcount;
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
                               const void *server_data, gpr_arena *arena) {
  return transport->vtable->init_stream(exec_ctx, transport, stream, refcount,
                                        server_data, arena);
}

void grpc_transport_perform_stream_op(grpc_exec_ctx *exec_ctx,
                                      grpc_transport *transport,
                                      grpc_stream *stream,
                                      grpc_transport_stream_op_batch *op) {
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
                                   grpc_stream *stream,
                                   grpc_closure *then_schedule_closure) {
  transport->vtable->destroy_stream(exec_ctx, transport, stream,
                                    then_schedule_closure);
}

char *grpc_transport_get_peer(grpc_exec_ctx *exec_ctx,
                              grpc_transport *transport) {
  return transport->vtable->get_peer(exec_ctx, transport);
}

grpc_endpoint *grpc_transport_get_endpoint(grpc_exec_ctx *exec_ctx,
                                           grpc_transport *transport) {
  return transport->vtable->get_endpoint(exec_ctx, transport);
}

// This comment should be sung to the tune of
// "Supercalifragilisticexpialidocious":
//
// grpc_transport_stream_op_batch_finish_with_failure
// is a function that must always unref cancel_error
// though it lives in lib, it handles transport stream ops sure
// it's grpc_transport_stream_op_batch_finish_with_failure

void grpc_transport_stream_op_batch_finish_with_failure(
    grpc_exec_ctx *exec_ctx, grpc_transport_stream_op_batch *batch,
    grpc_error *error) {
  if (batch->send_message) {
    grpc_byte_stream_destroy(exec_ctx,
                             batch->payload->send_message.send_message);
  }
  if (batch->recv_message) {
    GRPC_CLOSURE_SCHED(exec_ctx,
                       batch->payload->recv_message.recv_message_ready,
                       GRPC_ERROR_REF(error));
  }
  if (batch->recv_initial_metadata) {
    GRPC_CLOSURE_SCHED(
        exec_ctx,
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready,
        GRPC_ERROR_REF(error));
  }
  GRPC_CLOSURE_SCHED(exec_ctx, batch->on_complete, error);
  if (batch->cancel_stream) {
    GRPC_ERROR_UNREF(batch->payload->cancel_stream.cancel_error);
  }
}

typedef struct {
  grpc_closure outer_on_complete;
  grpc_closure *inner_on_complete;
  grpc_transport_op op;
} made_transport_op;

static void destroy_made_transport_op(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_error *error) {
  made_transport_op *op = arg;
  GRPC_CLOSURE_SCHED(exec_ctx, op->inner_on_complete, GRPC_ERROR_REF(error));
  gpr_free(op);
}

grpc_transport_op *grpc_make_transport_op(grpc_closure *on_complete) {
  made_transport_op *op = gpr_malloc(sizeof(*op));
  GRPC_CLOSURE_INIT(&op->outer_on_complete, destroy_made_transport_op, op,
                    grpc_schedule_on_exec_ctx);
  op->inner_on_complete = on_complete;
  memset(&op->op, 0, sizeof(op->op));
  op->op.on_consumed = &op->outer_on_complete;
  return &op->op;
}

typedef struct {
  grpc_closure outer_on_complete;
  grpc_closure *inner_on_complete;
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload payload;
} made_transport_stream_op;

static void destroy_made_transport_stream_op(grpc_exec_ctx *exec_ctx, void *arg,
                                             grpc_error *error) {
  made_transport_stream_op *op = arg;
  grpc_closure *c = op->inner_on_complete;
  gpr_free(op);
  GRPC_CLOSURE_RUN(exec_ctx, c, GRPC_ERROR_REF(error));
}

grpc_transport_stream_op_batch *grpc_make_transport_stream_op(
    grpc_closure *on_complete) {
  made_transport_stream_op *op = gpr_zalloc(sizeof(*op));
  op->op.payload = &op->payload;
  GRPC_CLOSURE_INIT(&op->outer_on_complete, destroy_made_transport_stream_op,
                    op, grpc_schedule_on_exec_ctx);
  op->inner_on_complete = on_complete;
  op->op.on_complete = &op->outer_on_complete;
  return &op->op;
}
