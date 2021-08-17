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

#include "src/core/lib/transport/transport.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/transport_impl.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_stream_refcount(false,
                                                         "stream_refcount");

void grpc_stream_destroy(grpc_stream_refcount* refcount) {
  if (!grpc_iomgr_is_any_background_poller_thread() &&
      (grpc_core::ExecCtx::Get()->flags() &
       GRPC_EXEC_CTX_FLAG_THREAD_RESOURCE_LOOP)) {
    /* Ick.
       The thread we're running on MAY be owned (indirectly) by a call-stack.
       If that's the case, destroying the call-stack MAY try to destroy the
       thread, which is a tangled mess that we just don't want to ever have to
       cope with.
       Throw this over to the executor (on a core-owned thread) and process it
       there. */
    grpc_core::Executor::Run(&refcount->destroy, GRPC_ERROR_NONE);
  } else {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &refcount->destroy,
                            GRPC_ERROR_NONE);
  }
}

void slice_stream_destroy(void* arg) {
  grpc_stream_destroy(static_cast<grpc_stream_refcount*>(arg));
}

#define STREAM_REF_FROM_SLICE_REF(p)         \
  ((grpc_stream_refcount*)(((uint8_t*)(p)) - \
                           offsetof(grpc_stream_refcount, slice_refcount)))

grpc_slice grpc_slice_from_stream_owned_buffer(grpc_stream_refcount* refcount,
                                               void* buffer, size_t length) {
#ifndef NDEBUG
  grpc_stream_ref(STREAM_REF_FROM_SLICE_REF(&refcount->slice_refcount),
                  "slice");
#else
  grpc_stream_ref(STREAM_REF_FROM_SLICE_REF(&refcount->slice_refcount));
#endif
  grpc_slice res;
  res.refcount = &refcount->slice_refcount;
  res.data.refcounted.bytes = static_cast<uint8_t*>(buffer);
  res.data.refcounted.length = length;
  return res;
}

#ifndef NDEBUG
void grpc_stream_ref_init(grpc_stream_refcount* refcount, int /*initial_refs*/,
                          grpc_iomgr_cb_func cb, void* cb_arg,
                          const char* object_type) {
  refcount->object_type = object_type;
#else
void grpc_stream_ref_init(grpc_stream_refcount* refcount, int /*initial_refs*/,
                          grpc_iomgr_cb_func cb, void* cb_arg) {
#endif
  GRPC_CLOSURE_INIT(&refcount->destroy, cb, cb_arg, grpc_schedule_on_exec_ctx);

  new (&refcount->refs) grpc_core::RefCount(
      1, GRPC_TRACE_FLAG_ENABLED(grpc_trace_stream_refcount) ? "stream_refcount"
                                                             : nullptr);
  new (&refcount->slice_refcount) grpc_slice_refcount(
      grpc_slice_refcount::Type::REGULAR, &refcount->refs, slice_stream_destroy,
      refcount, &refcount->slice_refcount);
}

static void move64(uint64_t* from, uint64_t* to) {
  *to += *from;
  *from = 0;
}

void grpc_transport_move_one_way_stats(grpc_transport_one_way_stats* from,
                                       grpc_transport_one_way_stats* to) {
  move64(&from->framing_bytes, &to->framing_bytes);
  move64(&from->data_bytes, &to->data_bytes);
  move64(&from->header_bytes, &to->header_bytes);
}

void grpc_transport_move_stats(grpc_transport_stream_stats* from,
                               grpc_transport_stream_stats* to) {
  grpc_transport_move_one_way_stats(&from->incoming, &to->incoming);
  grpc_transport_move_one_way_stats(&from->outgoing, &to->outgoing);
}

size_t grpc_transport_stream_size(grpc_transport* transport) {
  return GPR_ROUND_UP_TO_ALIGNMENT_SIZE(transport->vtable->sizeof_stream);
}

void grpc_transport_destroy(grpc_transport* transport) {
  transport->vtable->destroy(transport);
}

int grpc_transport_init_stream(grpc_transport* transport, grpc_stream* stream,
                               grpc_stream_refcount* refcount,
                               const void* server_data,
                               grpc_core::Arena* arena) {
  return transport->vtable->init_stream(transport, stream, refcount,
                                        server_data, arena);
}

void grpc_transport_perform_stream_op(grpc_transport* transport,
                                      grpc_stream* stream,
                                      grpc_transport_stream_op_batch* op) {
  transport->vtable->perform_stream_op(transport, stream, op);
}

void grpc_transport_perform_op(grpc_transport* transport,
                               grpc_transport_op* op) {
  transport->vtable->perform_op(transport, op);
}

void grpc_transport_set_pops(grpc_transport* transport, grpc_stream* stream,
                             grpc_polling_entity* pollent) {
  grpc_pollset* pollset;
  grpc_pollset_set* pollset_set;
  if ((pollset = grpc_polling_entity_pollset(pollent)) != nullptr) {
    transport->vtable->set_pollset(transport, stream, pollset);
  } else if ((pollset_set = grpc_polling_entity_pollset_set(pollent)) !=
             nullptr) {
    transport->vtable->set_pollset_set(transport, stream, pollset_set);
  } else {
    // No-op for empty pollset. Empty pollset is possible when using
    // non-fd-based event engines such as CFStream.
  }
}

void grpc_transport_destroy_stream(grpc_transport* transport,
                                   grpc_stream* stream,
                                   grpc_closure* then_schedule_closure) {
  transport->vtable->destroy_stream(transport, stream, then_schedule_closure);
}

grpc_endpoint* grpc_transport_get_endpoint(grpc_transport* transport) {
  return transport->vtable->get_endpoint(transport);
}

// This comment should be sung to the tune of
// "Supercalifragilisticexpialidocious":
//
// grpc_transport_stream_op_batch_finish_with_failure
// is a function that must always unref cancel_error
// though it lives in lib, it handles transport stream ops sure
// it's grpc_transport_stream_op_batch_finish_with_failure
void grpc_transport_stream_op_batch_finish_with_failure(
    grpc_transport_stream_op_batch* batch, grpc_error_handle error,
    grpc_core::CallCombiner* call_combiner) {
  if (batch->send_message) {
    batch->payload->send_message.send_message.reset();
  }
  if (batch->cancel_stream) {
    GRPC_ERROR_UNREF(batch->payload->cancel_stream.cancel_error);
  }
  // Construct a list of closures to execute.
  grpc_core::CallCombinerClosureList closures;
  if (batch->recv_initial_metadata) {
    closures.Add(
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready,
        GRPC_ERROR_REF(error), "failing recv_initial_metadata_ready");
  }
  if (batch->recv_message) {
    closures.Add(batch->payload->recv_message.recv_message_ready,
                 GRPC_ERROR_REF(error), "failing recv_message_ready");
  }
  if (batch->recv_trailing_metadata) {
    closures.Add(
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready,
        GRPC_ERROR_REF(error), "failing recv_trailing_metadata_ready");
  }
  if (batch->on_complete != nullptr) {
    closures.Add(batch->on_complete, GRPC_ERROR_REF(error),
                 "failing on_complete");
  }
  // Execute closures.
  closures.RunClosures(call_combiner);
  GRPC_ERROR_UNREF(error);
}

struct made_transport_op {
  grpc_closure outer_on_complete;
  grpc_closure* inner_on_complete = nullptr;
  grpc_transport_op op;
  made_transport_op() {
    memset(&outer_on_complete, 0, sizeof(outer_on_complete));
  }
};

static void destroy_made_transport_op(void* arg, grpc_error_handle error) {
  made_transport_op* op = static_cast<made_transport_op*>(arg);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->inner_on_complete,
                          GRPC_ERROR_REF(error));
  delete op;
}

grpc_transport_op* grpc_make_transport_op(grpc_closure* on_complete) {
  made_transport_op* op = new made_transport_op();
  GRPC_CLOSURE_INIT(&op->outer_on_complete, destroy_made_transport_op, op,
                    grpc_schedule_on_exec_ctx);
  op->inner_on_complete = on_complete;
  op->op.on_consumed = &op->outer_on_complete;
  return &op->op;
}

struct made_transport_stream_op {
  grpc_closure outer_on_complete;
  grpc_closure* inner_on_complete;
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload payload;
};
static void destroy_made_transport_stream_op(void* arg,
                                             grpc_error_handle error) {
  made_transport_stream_op* op = static_cast<made_transport_stream_op*>(arg);
  grpc_closure* c = op->inner_on_complete;
  gpr_free(op);
  grpc_core::Closure::Run(DEBUG_LOCATION, c, GRPC_ERROR_REF(error));
}

grpc_transport_stream_op_batch* grpc_make_transport_stream_op(
    grpc_closure* on_complete) {
  made_transport_stream_op* op =
      static_cast<made_transport_stream_op*>(gpr_zalloc(sizeof(*op)));
  op->op.payload = &op->payload;
  GRPC_CLOSURE_INIT(&op->outer_on_complete, destroy_made_transport_stream_op,
                    op, grpc_schedule_on_exec_ctx);
  op->inner_on_complete = on_complete;
  op->op.on_complete = &op->outer_on_complete;
  return &op->op;
}
