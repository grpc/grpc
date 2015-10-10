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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/channel/channel_stack.h"
#include "src/core/iomgr/alarm.h"
#include "src/core/profiling/timers.h"
#include "src/core/support/string.h"
#include "src/core/surface/api_trace.h"
#include "src/core/surface/byte_buffer_queue.h"
#include "src/core/surface/call.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/completion_queue.h"

/** The maximum number of completions possible.
    Based upon the maximum number of individually queueable ops in the batch
   api:
      - initial metadata send
      - message send
      - status/close send (depending on client/server)
      - initial metadata recv
      - message recv
      - status/close recv (depending on client/server) */
#define MAX_CONCURRENT_COMPLETIONS 6

typedef struct {
  grpc_ioreq_completion_func on_complete;
  void *user_data;
  int success;
} completed_request;

/* See request_set in grpc_call below for a description */
#define REQSET_EMPTY 'X'
#define REQSET_DONE 'Y'

#define MAX_SEND_INITIAL_METADATA_COUNT 3

typedef struct {
  /* Overall status of the operation: starts OK, may degrade to
     non-OK */
  gpr_uint8 success;
  /* a bit mask of which request ops are needed (1u << opid) */
  gpr_uint16 need_mask;
  /* a bit mask of which request ops are now completed */
  gpr_uint16 complete_mask;
  /* Completion function to call at the end of the operation */
  grpc_ioreq_completion_func on_complete;
  void *user_data;
} reqinfo_master;

/* Status data for a request can come from several sources; this
   enumerates them all, and acts as a priority sorting for which
   status to return to the application - earlier entries override
   later ones */
typedef enum {
  /* Status came from the application layer overriding whatever
     the wire says */
  STATUS_FROM_API_OVERRIDE = 0,
  /* Status was created by some internal channel stack operation */
  STATUS_FROM_CORE,
  /* Status came from 'the wire' - or somewhere below the surface
     layer */
  STATUS_FROM_WIRE,
  /* Status came from the server sending status */
  STATUS_FROM_SERVER_STATUS,
  STATUS_SOURCE_COUNT
} status_source;

typedef struct {
  gpr_uint8 is_set;
  grpc_status_code code;
  grpc_mdstr *details;
} received_status;

/* How far through the GRPC stream have we read? */
typedef enum {
  /* We are still waiting for initial metadata to complete */
  READ_STATE_INITIAL = 0,
  /* We have gotten initial metadata, and are reading either
     messages or trailing metadata */
  READ_STATE_GOT_INITIAL_METADATA,
  /* The stream is closed for reading */
  READ_STATE_READ_CLOSED,
  /* The stream is closed for reading & writing */
  READ_STATE_STREAM_CLOSED
} read_state;

typedef enum {
  WRITE_STATE_INITIAL = 0,
  WRITE_STATE_STARTED,
  WRITE_STATE_WRITE_CLOSED
} write_state;

struct grpc_call {
  grpc_completion_queue *cq;
  grpc_channel *channel;
  grpc_call *parent;
  grpc_call *first_child;
  grpc_mdctx *metadata_context;
  /* TODO(ctiller): share with cq if possible? */
  gpr_mu mu;
  gpr_mu completion_mu;

  /* how far through the stream have we read? */
  read_state read_state;
  /* how far through the stream have we written? */
  write_state write_state;
  /* client or server call */
  gpr_uint8 is_client;
  /* is the alarm set */
  gpr_uint8 have_alarm;
  /* are we currently performing a send operation */
  gpr_uint8 sending;
  /* are we currently performing a recv operation */
  gpr_uint8 receiving;
  /* are we currently completing requests */
  gpr_uint8 completing;
  /** has grpc_call_destroy been called */
  gpr_uint8 destroy_called;
  /* pairs with completed_requests */
  gpr_uint8 num_completed_requests;
  /* are we currently reading a message? */
  gpr_uint8 reading_message;
  /* have we bound a pollset yet? */
  gpr_uint8 bound_pollset;
  /* is an error status set */
  gpr_uint8 error_status_set;
  /** bitmask of allocated completion events in completions */
  gpr_uint8 allocated_completions;
  /** flag indicating that cancellation is inherited */
  gpr_uint8 cancellation_is_inherited;

  /* flags with bits corresponding to write states allowing us to determine
     what was sent */
  gpr_uint16 last_send_contains;
  /* cancel with this status on the next outgoing transport op */
  grpc_status_code cancel_with_status;

  /* Active ioreqs.
     request_set and request_data contain one element per active ioreq
     operation.

     request_set[op] is an integer specifying a set of operations to which
     the request belongs:
     - if it is < GRPC_IOREQ_OP_COUNT, then this operation is pending
     completion, and the integer represents to which group of operations
     the ioreq belongs. Each group is represented by one master, and the
     integer in request_set is an index into masters to find the master
     data.
     - if it is REQSET_EMPTY, the ioreq op is inactive and available to be
     started
     - finally, if request_set[op] is REQSET_DONE, then the operation is
     complete and unavailable to be started again

     request_data[op] is the request data as supplied by the initiator of
     a request, and is valid iff request_set[op] <= GRPC_IOREQ_OP_COUNT.
     The set fields are as per the request type specified by op.

     Finally, one element of masters is set per active _set_ of ioreq
     operations. It describes work left outstanding, result status, and
     what work to perform upon operation completion. As one ioreq of each
     op type can be active at once, by convention we choose the first element
     of the group to be the master -- ie the master of in-progress operation
     op is masters[request_set[op]]. This allows constant time allocation
     and a strong upper bound of a count of masters to be calculated. */
  gpr_uint8 request_set[GRPC_IOREQ_OP_COUNT];
  grpc_ioreq_data request_data[GRPC_IOREQ_OP_COUNT];
  gpr_uint32 request_flags[GRPC_IOREQ_OP_COUNT];
  reqinfo_master masters[GRPC_IOREQ_OP_COUNT];

  /* Dynamic array of ioreq's that have completed: the count of
     elements is queued in num_completed_requests.
     This list is built up under lock(), and flushed entirely during
     unlock().
     We know the upper bound of the number of elements as we can only
     have one ioreq of each type active at once. */
  completed_request completed_requests[GRPC_IOREQ_OP_COUNT];
  /* Incoming buffer of messages */
  grpc_byte_buffer_queue incoming_queue;
  /* Buffered read metadata waiting to be returned to the application.
     Element 0 is initial metadata, element 1 is trailing metadata. */
  grpc_metadata_array buffered_metadata[2];
  /* All metadata received - unreffed at once at the end of the call */
  grpc_mdelem **owned_metadata;
  size_t owned_metadata_count;
  size_t owned_metadata_capacity;

  /* Received call statuses from various sources */
  received_status status[STATUS_SOURCE_COUNT];

  /* Compression algorithm for the call */
  grpc_compression_algorithm compression_algorithm;

  /* Supported encodings (compression algorithms), a bitset */
  gpr_uint32 encodings_accepted_by_peer;

  /* Contexts for various subsystems (security, tracing, ...). */
  grpc_call_context_element context[GRPC_CONTEXT_COUNT];

  /* Deadline alarm - if have_alarm is non-zero */
  grpc_alarm alarm;

  /* Call refcount - to keep the call alive during asynchronous operations */
  gpr_refcount internal_refcount;

  grpc_linked_mdelem send_initial_metadata[MAX_SEND_INITIAL_METADATA_COUNT];
  grpc_linked_mdelem status_link;
  grpc_linked_mdelem details_link;
  size_t send_initial_metadata_count;
  gpr_timespec send_deadline;

  grpc_stream_op_buffer send_ops;
  grpc_stream_op_buffer recv_ops;
  grpc_stream_state recv_state;

  gpr_slice_buffer incoming_message;
  gpr_uint32 incoming_message_length;
  gpr_uint32 incoming_message_flags;
  grpc_closure destroy_closure;
  grpc_closure on_done_recv;
  grpc_closure on_done_send;
  grpc_closure on_done_bind;

  /** completion events - for completion queue use */
  grpc_cq_completion completions[MAX_CONCURRENT_COMPLETIONS];

  /** siblings: children of the same parent form a list, and this list is
     protected under
      parent->mu */
  grpc_call *sibling_next;
  grpc_call *sibling_prev;
};

#define CALL_STACK_FROM_CALL(call) ((grpc_call_stack *)((call) + 1))
#define CALL_FROM_CALL_STACK(call_stack) (((grpc_call *)(call_stack)) - 1)
#define CALL_ELEM_FROM_CALL(call, idx) \
  grpc_call_stack_element(CALL_STACK_FROM_CALL(call), idx)
#define CALL_FROM_TOP_ELEM(top_elem) \
  CALL_FROM_CALL_STACK(grpc_call_stack_from_top_element(top_elem))

static void set_deadline_alarm(grpc_exec_ctx *exec_ctx, grpc_call *call,
                               gpr_timespec deadline);
static void call_on_done_recv(grpc_exec_ctx *exec_ctx, void *call, int success);
static void call_on_done_send(grpc_exec_ctx *exec_ctx, void *call, int success);
static int fill_send_ops(grpc_call *call, grpc_transport_stream_op *op);
static void execute_op(grpc_exec_ctx *exec_ctx, grpc_call *call,
                       grpc_transport_stream_op *op);
static void recv_metadata(grpc_exec_ctx *exec_ctx, grpc_call *call,
                          grpc_metadata_batch *metadata);
static void finish_read_ops(grpc_call *call);
static grpc_call_error cancel_with_status(grpc_call *c, grpc_status_code status,
                                          const char *description);
static void finished_loose_op(grpc_exec_ctx *exec_ctx, void *call, int success);

static void lock(grpc_call *call);
static void unlock(grpc_exec_ctx *exec_ctx, grpc_call *call);

grpc_call *grpc_call_create(grpc_channel *channel, grpc_call *parent_call,
                            gpr_uint32 propagation_mask,
                            grpc_completion_queue *cq,
                            const void *server_transport_data,
                            grpc_mdelem **add_initial_metadata,
                            size_t add_initial_metadata_count,
                            gpr_timespec send_deadline) {
  size_t i;
  grpc_transport_stream_op initial_op;
  grpc_transport_stream_op *initial_op_ptr = NULL;
  grpc_channel_stack *channel_stack = grpc_channel_get_channel_stack(channel);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call *call;
  GPR_TIMER_BEGIN("grpc_call_create", 0);
  call = gpr_malloc(sizeof(grpc_call) + channel_stack->call_stack_size);
  memset(call, 0, sizeof(grpc_call));
  gpr_mu_init(&call->mu);
  gpr_mu_init(&call->completion_mu);
  call->channel = channel;
  call->cq = cq;
  if (cq != NULL) {
    GRPC_CQ_INTERNAL_REF(cq, "bind");
  }
  call->parent = parent_call;
  call->is_client = server_transport_data == NULL;
  for (i = 0; i < GRPC_IOREQ_OP_COUNT; i++) {
    call->request_set[i] = REQSET_EMPTY;
  }
  if (call->is_client) {
    call->request_set[GRPC_IOREQ_SEND_TRAILING_METADATA] = REQSET_DONE;
    call->request_set[GRPC_IOREQ_SEND_STATUS] = REQSET_DONE;
  }
  GPR_ASSERT(add_initial_metadata_count < MAX_SEND_INITIAL_METADATA_COUNT);
  for (i = 0; i < add_initial_metadata_count; i++) {
    call->send_initial_metadata[i].md = add_initial_metadata[i];
  }
  call->send_initial_metadata_count = add_initial_metadata_count;
  call->send_deadline = send_deadline;
  GRPC_CHANNEL_INTERNAL_REF(channel, "call");
  call->metadata_context = grpc_channel_get_metadata_context(channel);
  grpc_sopb_init(&call->send_ops);
  grpc_sopb_init(&call->recv_ops);
  gpr_slice_buffer_init(&call->incoming_message);
  grpc_closure_init(&call->on_done_recv, call_on_done_recv, call);
  grpc_closure_init(&call->on_done_send, call_on_done_send, call);
  grpc_closure_init(&call->on_done_bind, finished_loose_op, call);
  /* dropped in destroy and when READ_STATE_STREAM_CLOSED received */
  gpr_ref_init(&call->internal_refcount, 2);
  /* server hack: start reads immediately so we can get initial metadata.
     TODO(ctiller): figure out a cleaner solution */
  if (!call->is_client) {
    memset(&initial_op, 0, sizeof(initial_op));
    initial_op.recv_ops = &call->recv_ops;
    initial_op.recv_state = &call->recv_state;
    initial_op.on_done_recv = &call->on_done_recv;
    initial_op.context = call->context;
    call->receiving = 1;
    GRPC_CALL_INTERNAL_REF(call, "receiving");
    initial_op_ptr = &initial_op;
  }
  grpc_call_stack_init(&exec_ctx, channel_stack, server_transport_data,
                       initial_op_ptr, CALL_STACK_FROM_CALL(call));
  if (parent_call != NULL) {
    GRPC_CALL_INTERNAL_REF(parent_call, "child");
    GPR_ASSERT(call->is_client);
    GPR_ASSERT(!parent_call->is_client);

    gpr_mu_lock(&parent_call->mu);

    if (propagation_mask & GRPC_PROPAGATE_DEADLINE) {
      send_deadline = gpr_time_min(
          gpr_convert_clock_type(send_deadline,
                                 parent_call->send_deadline.clock_type),
          parent_call->send_deadline);
    }
    /* for now GRPC_PROPAGATE_TRACING_CONTEXT *MUST* be passed with
     * GRPC_PROPAGATE_STATS_CONTEXT */
    /* TODO(ctiller): This should change to use the appropriate census start_op
     * call. */
    if (propagation_mask & GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT) {
      GPR_ASSERT(propagation_mask & GRPC_PROPAGATE_CENSUS_STATS_CONTEXT);
      grpc_call_context_set(call, GRPC_CONTEXT_TRACING,
                            parent_call->context[GRPC_CONTEXT_TRACING].value,
                            NULL);
    } else {
      GPR_ASSERT(propagation_mask & GRPC_PROPAGATE_CENSUS_STATS_CONTEXT);
    }
    if (propagation_mask & GRPC_PROPAGATE_CANCELLATION) {
      call->cancellation_is_inherited = 1;
    }

    if (parent_call->first_child == NULL) {
      parent_call->first_child = call;
      call->sibling_next = call->sibling_prev = call;
    } else {
      call->sibling_next = parent_call->first_child;
      call->sibling_prev = parent_call->first_child->sibling_prev;
      call->sibling_next->sibling_prev = call->sibling_prev->sibling_next =
          call;
    }

    gpr_mu_unlock(&parent_call->mu);
  }
  if (gpr_time_cmp(send_deadline, gpr_inf_future(send_deadline.clock_type)) !=
      0) {
    set_deadline_alarm(&exec_ctx, call, send_deadline);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_TIMER_END("grpc_call_create", 0);
  return call;
}

void grpc_call_set_completion_queue(grpc_exec_ctx *exec_ctx, grpc_call *call,
                                    grpc_completion_queue *cq) {
  lock(call);
  call->cq = cq;
  if (cq) {
    GRPC_CQ_INTERNAL_REF(cq, "bind");
  }
  unlock(exec_ctx, call);
}

grpc_completion_queue *grpc_call_get_completion_queue(grpc_call *call) {
  return call->cq;
}

static grpc_cq_completion *allocate_completion(grpc_call *call) {
  gpr_uint8 i;
  gpr_mu_lock(&call->completion_mu);
  for (i = 0; i < GPR_ARRAY_SIZE(call->completions); i++) {
    if (call->allocated_completions & (1u << i)) {
      continue;
    }
    /* NB: the following integer arithmetic operation needs to be in its
     * expanded form due to the "integral promotion" performed (see section
     * 3.2.1.1 of the C89 draft standard). A cast to the smaller container type
     * is then required to avoid the compiler warning */
    call->allocated_completions =
        (gpr_uint8)(call->allocated_completions | (1u << i));
    gpr_mu_unlock(&call->completion_mu);
    return &call->completions[i];
  }
  GPR_UNREACHABLE_CODE(return NULL);
  return NULL;
}

static void done_completion(grpc_exec_ctx *exec_ctx, void *call,
                            grpc_cq_completion *completion) {
  grpc_call *c = call;
  gpr_mu_lock(&c->completion_mu);
  c->allocated_completions &=
      (gpr_uint8) ~(1u << (completion - c->completions));
  gpr_mu_unlock(&c->completion_mu);
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, c, "completion");
}

#ifdef GRPC_CALL_REF_COUNT_DEBUG
void grpc_call_internal_ref(grpc_call *c, const char *reason) {
  gpr_log(GPR_DEBUG, "CALL:   ref %p %d -> %d [%s]", c,
          c->internal_refcount.count, c->internal_refcount.count + 1, reason);
#else
void grpc_call_internal_ref(grpc_call *c) {
#endif
  gpr_ref(&c->internal_refcount);
}

static void destroy_call(grpc_exec_ctx *exec_ctx, grpc_call *call) {
  size_t i;
  grpc_call *c = call;
  GPR_TIMER_BEGIN("destroy_call", 0);
  grpc_call_stack_destroy(exec_ctx, CALL_STACK_FROM_CALL(c));
  GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, c->channel, "call");
  gpr_mu_destroy(&c->mu);
  gpr_mu_destroy(&c->completion_mu);
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (c->status[i].details) {
      GRPC_MDSTR_UNREF(c->status[i].details);
    }
  }
  for (i = 0; i < c->owned_metadata_count; i++) {
    GRPC_MDELEM_UNREF(c->owned_metadata[i]);
  }
  gpr_free(c->owned_metadata);
  for (i = 0; i < GPR_ARRAY_SIZE(c->buffered_metadata); i++) {
    gpr_free(c->buffered_metadata[i].metadata);
  }
  for (i = 0; i < c->send_initial_metadata_count; i++) {
    GRPC_MDELEM_UNREF(c->send_initial_metadata[i].md);
  }
  for (i = 0; i < GRPC_CONTEXT_COUNT; i++) {
    if (c->context[i].destroy) {
      c->context[i].destroy(c->context[i].value);
    }
  }
  grpc_sopb_destroy(&c->send_ops);
  grpc_sopb_destroy(&c->recv_ops);
  grpc_bbq_destroy(&c->incoming_queue);
  gpr_slice_buffer_destroy(&c->incoming_message);
  if (c->cq) {
    GRPC_CQ_INTERNAL_UNREF(c->cq, "bind");
  }
  gpr_free(c);
  GPR_TIMER_END("destroy_call", 0);
}

#ifdef GRPC_CALL_REF_COUNT_DEBUG
void grpc_call_internal_unref(grpc_exec_ctx *exec_ctx, grpc_call *c,
                              const char *reason) {
  gpr_log(GPR_DEBUG, "CALL: unref %p %d -> %d [%s]", c,
          c->internal_refcount.count, c->internal_refcount.count - 1, reason);
#else
void grpc_call_internal_unref(grpc_exec_ctx *exec_ctx, grpc_call *c) {
#endif
  if (gpr_unref(&c->internal_refcount)) {
    destroy_call(exec_ctx, c);
  }
}

static void set_status_code(grpc_call *call, status_source source,
                            gpr_uint32 status) {
  if (call->status[source].is_set) return;

  call->status[source].is_set = 1;
  call->status[source].code = (grpc_status_code)status;
  call->error_status_set = status != GRPC_STATUS_OK;

  if (status != GRPC_STATUS_OK && !grpc_bbq_empty(&call->incoming_queue)) {
    grpc_bbq_flush(&call->incoming_queue);
  }
}

static void set_compression_algorithm(grpc_call *call,
                                      grpc_compression_algorithm algo) {
  call->compression_algorithm = algo;
}

grpc_compression_algorithm grpc_call_test_only_get_compression_algorithm(
    grpc_call *call) {
  grpc_compression_algorithm algorithm;
  gpr_mu_lock(&call->mu);
  algorithm = call->compression_algorithm;
  gpr_mu_unlock(&call->mu);
  return algorithm;
}

static void set_encodings_accepted_by_peer(
    grpc_call *call, const gpr_slice accept_encoding_slice) {
  size_t i;
  grpc_compression_algorithm algorithm;
  gpr_slice_buffer accept_encoding_parts;

  gpr_slice_buffer_init(&accept_encoding_parts);
  gpr_slice_split(accept_encoding_slice, ",", &accept_encoding_parts);

  /* No need to zero call->encodings_accepted_by_peer: grpc_call_create already
   * zeroes the whole grpc_call */
  /* Always support no compression */
  GPR_BITSET(&call->encodings_accepted_by_peer, GRPC_COMPRESS_NONE);
  for (i = 0; i < accept_encoding_parts.count; i++) {
    const gpr_slice *accept_encoding_entry_slice =
        &accept_encoding_parts.slices[i];
    if (grpc_compression_algorithm_parse(
            (const char *)GPR_SLICE_START_PTR(*accept_encoding_entry_slice),
            GPR_SLICE_LENGTH(*accept_encoding_entry_slice), &algorithm)) {
      GPR_BITSET(&call->encodings_accepted_by_peer, algorithm);
    } else {
      char *accept_encoding_entry_str =
          gpr_dump_slice(*accept_encoding_entry_slice, GPR_DUMP_ASCII);
      gpr_log(GPR_ERROR,
              "Invalid entry in accept encoding metadata: '%s'. Ignoring.",
              accept_encoding_entry_str);
      gpr_free(accept_encoding_entry_str);
    }
  }
}

gpr_uint32 grpc_call_test_only_get_encodings_accepted_by_peer(grpc_call *call) {
  gpr_uint32 encodings_accepted_by_peer;
  gpr_mu_lock(&call->mu);
  encodings_accepted_by_peer = call->encodings_accepted_by_peer;
  gpr_mu_unlock(&call->mu);
  return encodings_accepted_by_peer;
}

gpr_uint32 grpc_call_test_only_get_message_flags(grpc_call *call) {
  gpr_uint32 flags;
  gpr_mu_lock(&call->mu);
  flags = call->incoming_message_flags;
  gpr_mu_unlock(&call->mu);
  return flags;
}

static void set_status_details(grpc_call *call, status_source source,
                               grpc_mdstr *status) {
  if (call->status[source].details != NULL) {
    GRPC_MDSTR_UNREF(call->status[source].details);
  }
  call->status[source].details = status;
}

static int is_op_live(grpc_call *call, grpc_ioreq_op op) {
  gpr_uint8 set = call->request_set[op];
  reqinfo_master *master;
  if (set >= GRPC_IOREQ_OP_COUNT) return 0;
  master = &call->masters[set];
  return (master->complete_mask & (1u << op)) == 0;
}

static void lock(grpc_call *call) { gpr_mu_lock(&call->mu); }

static int need_more_data(grpc_call *call) {
  if (call->read_state == READ_STATE_STREAM_CLOSED) return 0;
  /* TODO(ctiller): this needs some serious cleanup */
  return is_op_live(call, GRPC_IOREQ_RECV_INITIAL_METADATA) ||
         (is_op_live(call, GRPC_IOREQ_RECV_MESSAGE) &&
          grpc_bbq_empty(&call->incoming_queue)) ||
         is_op_live(call, GRPC_IOREQ_RECV_TRAILING_METADATA) ||
         is_op_live(call, GRPC_IOREQ_RECV_STATUS) ||
         is_op_live(call, GRPC_IOREQ_RECV_STATUS_DETAILS) ||
         (is_op_live(call, GRPC_IOREQ_RECV_CLOSE) &&
          grpc_bbq_empty(&call->incoming_queue)) ||
         (call->write_state == WRITE_STATE_INITIAL && !call->is_client) ||
         (call->cancel_with_status != GRPC_STATUS_OK) || call->destroy_called;
}

static void unlock(grpc_exec_ctx *exec_ctx, grpc_call *call) {
  grpc_transport_stream_op op;
  completed_request completed_requests[GRPC_IOREQ_OP_COUNT];
  int completing_requests = 0;
  int start_op = 0;
  int i;
  const size_t MAX_RECV_PEEK_AHEAD = 65536;
  size_t buffered_bytes;

  GPR_TIMER_BEGIN("unlock", 0);

  memset(&op, 0, sizeof(op));

  op.cancel_with_status = call->cancel_with_status;
  start_op = op.cancel_with_status != GRPC_STATUS_OK;
  call->cancel_with_status = GRPC_STATUS_OK; /* reset */

  if (!call->receiving && need_more_data(call)) {
    if (grpc_bbq_empty(&call->incoming_queue) && call->reading_message) {
      op.max_recv_bytes = call->incoming_message_length -
                          call->incoming_message.length + MAX_RECV_PEEK_AHEAD;
    } else {
      buffered_bytes = grpc_bbq_bytes(&call->incoming_queue);
      if (buffered_bytes > MAX_RECV_PEEK_AHEAD) {
        op.max_recv_bytes = 0;
      } else {
        op.max_recv_bytes = MAX_RECV_PEEK_AHEAD - buffered_bytes;
      }
    }
    /* TODO(ctiller): 1024 is basically to cover a bug
       I don't understand yet */
    if (op.max_recv_bytes > 1024) {
      op.recv_ops = &call->recv_ops;
      op.recv_state = &call->recv_state;
      op.on_done_recv = &call->on_done_recv;
      call->receiving = 1;
      GRPC_CALL_INTERNAL_REF(call, "receiving");
      start_op = 1;
    }
  }

  if (!call->sending) {
    if (fill_send_ops(call, &op)) {
      call->sending = 1;
      GRPC_CALL_INTERNAL_REF(call, "sending");
      start_op = 1;
    }
  }

  if (!call->bound_pollset && call->cq && (!call->is_client || start_op)) {
    call->bound_pollset = 1;
    op.bind_pollset = grpc_cq_pollset(call->cq);
    start_op = 1;
  }

  if (!call->completing && call->num_completed_requests != 0) {
    completing_requests = call->num_completed_requests;
    memcpy(completed_requests, call->completed_requests,
           sizeof(completed_requests));
    call->num_completed_requests = 0;
    call->completing = 1;
    GRPC_CALL_INTERNAL_REF(call, "completing");
  }

  gpr_mu_unlock(&call->mu);

  if (start_op) {
    execute_op(exec_ctx, call, &op);
  }

  if (completing_requests > 0) {
    for (i = 0; i < completing_requests; i++) {
      completed_requests[i].on_complete(exec_ctx, call,
                                        completed_requests[i].success,
                                        completed_requests[i].user_data);
    }
    lock(call);
    call->completing = 0;
    unlock(exec_ctx, call);
    GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "completing");
  }

  GPR_TIMER_END("unlock", 0);
}

static void get_final_status(grpc_call *call, grpc_ioreq_data out) {
  int i;
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (call->status[i].is_set) {
      out.recv_status.set_value(call->status[i].code,
                                out.recv_status.user_data);
      return;
    }
  }
  if (call->is_client) {
    out.recv_status.set_value(GRPC_STATUS_UNKNOWN, out.recv_status.user_data);
  } else {
    out.recv_status.set_value(GRPC_STATUS_OK, out.recv_status.user_data);
  }
}

static void get_final_details(grpc_call *call, grpc_ioreq_data out) {
  int i;
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (call->status[i].is_set) {
      if (call->status[i].details) {
        gpr_slice details = call->status[i].details->slice;
        size_t len = GPR_SLICE_LENGTH(details);
        if (len + 1 > *out.recv_status_details.details_capacity) {
          *out.recv_status_details.details_capacity = GPR_MAX(
              len + 1, *out.recv_status_details.details_capacity * 3 / 2);
          *out.recv_status_details.details =
              gpr_realloc(*out.recv_status_details.details,
                          *out.recv_status_details.details_capacity);
        }
        memcpy(*out.recv_status_details.details, GPR_SLICE_START_PTR(details),
               len);
        (*out.recv_status_details.details)[len] = 0;
      } else {
        goto no_details;
      }
      return;
    }
  }

no_details:
  if (0 == *out.recv_status_details.details_capacity) {
    *out.recv_status_details.details_capacity = 8;
    *out.recv_status_details.details =
        gpr_malloc(*out.recv_status_details.details_capacity);
  }
  **out.recv_status_details.details = 0;
}

static void finish_live_ioreq_op(grpc_call *call, grpc_ioreq_op op,
                                 int success) {
  completed_request *cr;
  gpr_uint8 master_set = call->request_set[op];
  reqinfo_master *master;
  size_t i;
  /* ioreq is live: we need to do something */
  master = &call->masters[master_set];
  /* NB: the following integer arithmetic operation needs to be in its
   * expanded form due to the "integral promotion" performed (see section
   * 3.2.1.1 of the C89 draft standard). A cast to the smaller container type
   * is then required to avoid the compiler warning */
  master->complete_mask = (gpr_uint16)(master->complete_mask | (1u << op));
  if (!success) {
    master->success = 0;
  }
  if (master->complete_mask == master->need_mask) {
    for (i = 0; i < GRPC_IOREQ_OP_COUNT; i++) {
      if (call->request_set[i] != master_set) {
        continue;
      }
      call->request_set[i] = REQSET_DONE;
      switch ((grpc_ioreq_op)i) {
        case GRPC_IOREQ_RECV_MESSAGE:
        case GRPC_IOREQ_SEND_MESSAGE:
          call->request_set[i] = REQSET_EMPTY;
          if (!master->success) {
            call->write_state = WRITE_STATE_WRITE_CLOSED;
          }
          break;
        case GRPC_IOREQ_SEND_STATUS:
          if (call->request_data[GRPC_IOREQ_SEND_STATUS].send_status.details !=
              NULL) {
            GRPC_MDSTR_UNREF(
                call->request_data[GRPC_IOREQ_SEND_STATUS].send_status.details);
            call->request_data[GRPC_IOREQ_SEND_STATUS].send_status.details =
                NULL;
          }
          break;
        case GRPC_IOREQ_RECV_CLOSE:
        case GRPC_IOREQ_SEND_INITIAL_METADATA:
        case GRPC_IOREQ_SEND_TRAILING_METADATA:
        case GRPC_IOREQ_SEND_CLOSE:
          break;
        case GRPC_IOREQ_RECV_STATUS:
          get_final_status(call, call->request_data[GRPC_IOREQ_RECV_STATUS]);
          break;
        case GRPC_IOREQ_RECV_STATUS_DETAILS:
          get_final_details(call,
                            call->request_data[GRPC_IOREQ_RECV_STATUS_DETAILS]);
          break;
        case GRPC_IOREQ_RECV_INITIAL_METADATA:
          GPR_SWAP(grpc_metadata_array, call->buffered_metadata[0],
                   *call->request_data[GRPC_IOREQ_RECV_INITIAL_METADATA]
                        .recv_metadata);
          break;
        case GRPC_IOREQ_RECV_TRAILING_METADATA:
          GPR_SWAP(grpc_metadata_array, call->buffered_metadata[1],
                   *call->request_data[GRPC_IOREQ_RECV_TRAILING_METADATA]
                        .recv_metadata);
          break;
        case GRPC_IOREQ_OP_COUNT:
          abort();
          break;
      }
    }
    cr = &call->completed_requests[call->num_completed_requests++];
    cr->success = master->success;
    cr->on_complete = master->on_complete;
    cr->user_data = master->user_data;
  }
}

static void finish_ioreq_op(grpc_call *call, grpc_ioreq_op op, int success) {
  if (is_op_live(call, op)) {
    finish_live_ioreq_op(call, op, success);
  }
}

static void early_out_write_ops(grpc_call *call) {
  switch (call->write_state) {
    case WRITE_STATE_WRITE_CLOSED:
      finish_ioreq_op(call, GRPC_IOREQ_SEND_MESSAGE, 0);
      finish_ioreq_op(call, GRPC_IOREQ_SEND_STATUS, 0);
      finish_ioreq_op(call, GRPC_IOREQ_SEND_TRAILING_METADATA, 0);
      finish_ioreq_op(call, GRPC_IOREQ_SEND_CLOSE, 1);
    /* fallthrough */
    case WRITE_STATE_STARTED:
      finish_ioreq_op(call, GRPC_IOREQ_SEND_INITIAL_METADATA, 0);
    /* fallthrough */
    case WRITE_STATE_INITIAL:
      /* do nothing */
      break;
  }
}

static void call_on_done_send(grpc_exec_ctx *exec_ctx, void *pc, int success) {
  grpc_call *call = pc;
  GPR_TIMER_BEGIN("call_on_done_send", 0);
  lock(call);
  if (call->last_send_contains & (1 << GRPC_IOREQ_SEND_INITIAL_METADATA)) {
    finish_ioreq_op(call, GRPC_IOREQ_SEND_INITIAL_METADATA, success);
    call->write_state = WRITE_STATE_STARTED;
  }
  if (call->last_send_contains & (1 << GRPC_IOREQ_SEND_MESSAGE)) {
    finish_ioreq_op(call, GRPC_IOREQ_SEND_MESSAGE, success);
  }
  if (call->last_send_contains & (1 << GRPC_IOREQ_SEND_CLOSE)) {
    finish_ioreq_op(call, GRPC_IOREQ_SEND_TRAILING_METADATA, success);
    finish_ioreq_op(call, GRPC_IOREQ_SEND_STATUS, success);
    finish_ioreq_op(call, GRPC_IOREQ_SEND_CLOSE, 1);
    call->write_state = WRITE_STATE_WRITE_CLOSED;
  }
  if (!success) {
    call->write_state = WRITE_STATE_WRITE_CLOSED;
    early_out_write_ops(call);
  }
  call->send_ops.nops = 0;
  call->last_send_contains = 0;
  call->sending = 0;
  unlock(exec_ctx, call);
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "sending");
  GPR_TIMER_END("call_on_done_send", 0);
}

static void finish_message(grpc_call *call) {
  GPR_TIMER_BEGIN("finish_message", 0);
  if (call->error_status_set == 0) {
    /* TODO(ctiller): this could be a lot faster if coded directly */
    grpc_byte_buffer *byte_buffer;
    /* some aliases for readability */
    gpr_slice *slices = call->incoming_message.slices;
    const size_t nslices = call->incoming_message.count;

    if ((call->incoming_message_flags & GRPC_WRITE_INTERNAL_COMPRESS) &&
        (call->compression_algorithm > GRPC_COMPRESS_NONE)) {
      byte_buffer = grpc_raw_compressed_byte_buffer_create(
          slices, nslices, call->compression_algorithm);
    } else {
      byte_buffer = grpc_raw_byte_buffer_create(slices, nslices);
    }
    grpc_bbq_push(&call->incoming_queue, byte_buffer);
  }
  gpr_slice_buffer_reset_and_unref(&call->incoming_message);
  GPR_ASSERT(call->incoming_message.count == 0);
  call->reading_message = 0;
  GPR_TIMER_END("finish_message", 0);
}

static int begin_message(grpc_call *call, grpc_begin_message msg) {
  /* can't begin a message when we're still reading a message */
  if (call->reading_message) {
    char *message = NULL;
    gpr_asprintf(
        &message, "Message terminated early; read %d bytes, expected %d",
        (int)call->incoming_message.length, (int)call->incoming_message_length);
    cancel_with_status(call, GRPC_STATUS_INVALID_ARGUMENT, message);
    gpr_free(message);
    return 0;
  }
  /* sanity check: if message flags indicate a compressed message, the
   * compression level should already be present in the call, as parsed off its
   * corresponding metadata. */
  if ((msg.flags & GRPC_WRITE_INTERNAL_COMPRESS) &&
      (call->compression_algorithm == GRPC_COMPRESS_NONE)) {
    char *message = NULL;
    char *alg_name;
    if (!grpc_compression_algorithm_name(call->compression_algorithm,
                                         &alg_name)) {
      /* This shouldn't happen, other than due to data corruption */
      alg_name = "<unknown>";
    }
    gpr_asprintf(&message,
                 "Invalid compression algorithm (%s) for compressed message.",
                 alg_name);
    cancel_with_status(call, GRPC_STATUS_INTERNAL, message);
    gpr_free(message);
    return 0;
  }
  /* stash away parameters, and prepare for incoming slices */
  if (msg.length > grpc_channel_get_max_message_length(call->channel)) {
    char *message = NULL;
    gpr_asprintf(
        &message,
        "Maximum message length of %d exceeded by a message of length %d",
        grpc_channel_get_max_message_length(call->channel), msg.length);
    cancel_with_status(call, GRPC_STATUS_INVALID_ARGUMENT, message);
    gpr_free(message);
    return 0;
  } else if (msg.length > 0) {
    call->reading_message = 1;
    call->incoming_message_length = msg.length;
    call->incoming_message_flags = msg.flags;
    return 1;
  } else {
    finish_message(call);
    return 1;
  }
}

static int add_slice_to_message(grpc_call *call, gpr_slice slice) {
  if (GPR_SLICE_LENGTH(slice) == 0) {
    gpr_slice_unref(slice);
    return 1;
  }
  /* we have to be reading a message to know what to do here */
  if (!call->reading_message) {
    gpr_slice_unref(slice);
    cancel_with_status(call, GRPC_STATUS_INVALID_ARGUMENT,
                       "Received payload data while not reading a message");
    return 0;
  }
  /* append the slice to the incoming buffer */
  gpr_slice_buffer_add(&call->incoming_message, slice);
  if (call->incoming_message.length > call->incoming_message_length) {
    /* if we got too many bytes, complain */
    char *message = NULL;
    gpr_asprintf(
        &message, "Receiving message overflow; read %d bytes, expected %d",
        (int)call->incoming_message.length, (int)call->incoming_message_length);
    cancel_with_status(call, GRPC_STATUS_INVALID_ARGUMENT, message);
    gpr_free(message);
    return 0;
  } else if (call->incoming_message.length == call->incoming_message_length) {
    finish_message(call);
    return 1;
  } else {
    return 1;
  }
}

static void call_on_done_recv(grpc_exec_ctx *exec_ctx, void *pc, int success) {
  grpc_call *call = pc;
  grpc_call *child_call;
  grpc_call *next_child_call;
  size_t i;
  GPR_TIMER_BEGIN("call_on_done_recv", 0);
  lock(call);
  call->receiving = 0;
  if (success) {
    for (i = 0; success && i < call->recv_ops.nops; i++) {
      grpc_stream_op *op = &call->recv_ops.ops[i];
      switch (op->type) {
        case GRPC_NO_OP:
          break;
        case GRPC_OP_METADATA:
          GPR_TIMER_BEGIN("recv_metadata", 0);
          recv_metadata(exec_ctx, call, &op->data.metadata);
          GPR_TIMER_END("recv_metadata", 0);
          break;
        case GRPC_OP_BEGIN_MESSAGE:
          GPR_TIMER_BEGIN("begin_message", 0);
          success = begin_message(call, op->data.begin_message);
          GPR_TIMER_END("begin_message", 0);
          break;
        case GRPC_OP_SLICE:
          GPR_TIMER_BEGIN("add_slice_to_message", 0);
          success = add_slice_to_message(call, op->data.slice);
          GPR_TIMER_END("add_slice_to_message", 0);
          break;
      }
    }
    if (!success) {
      grpc_stream_ops_unref_owned_objects(&call->recv_ops.ops[i],
                                          call->recv_ops.nops - i);
    }
    if (call->recv_state == GRPC_STREAM_RECV_CLOSED) {
      GPR_ASSERT(call->read_state <= READ_STATE_READ_CLOSED);
      call->read_state = READ_STATE_READ_CLOSED;
    }
    if (call->recv_state == GRPC_STREAM_CLOSED) {
      GPR_ASSERT(call->read_state <= READ_STATE_STREAM_CLOSED);
      call->read_state = READ_STATE_STREAM_CLOSED;
      if (call->have_alarm) {
        grpc_alarm_cancel(exec_ctx, &call->alarm);
      }
      /* propagate cancellation to any interested children */
      child_call = call->first_child;
      if (child_call != NULL) {
        do {
          next_child_call = child_call->sibling_next;
          if (child_call->cancellation_is_inherited) {
            GRPC_CALL_INTERNAL_REF(child_call, "propagate_cancel");
            grpc_call_cancel(child_call, NULL);
            GRPC_CALL_INTERNAL_UNREF(exec_ctx, child_call, "propagate_cancel");
          }
          child_call = next_child_call;
        } while (child_call != call->first_child);
      }
      GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "closed");
    }
    finish_read_ops(call);
  } else {
    finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, 0);
    finish_ioreq_op(call, GRPC_IOREQ_RECV_STATUS, 0);
    finish_ioreq_op(call, GRPC_IOREQ_RECV_CLOSE, 0);
    finish_ioreq_op(call, GRPC_IOREQ_RECV_TRAILING_METADATA, 0);
    finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, 0);
    finish_ioreq_op(call, GRPC_IOREQ_RECV_STATUS_DETAILS, 0);
  }
  call->recv_ops.nops = 0;
  unlock(exec_ctx, call);

  GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "receiving");
  GPR_TIMER_END("call_on_done_recv", 0);
}

static int prepare_application_metadata(grpc_call *call, size_t count,
                                        grpc_metadata *metadata) {
  size_t i;
  for (i = 0; i < count; i++) {
    grpc_metadata *md = &metadata[i];
    grpc_metadata *next_md = (i == count - 1) ? NULL : &metadata[i + 1];
    grpc_metadata *prev_md = (i == 0) ? NULL : &metadata[i - 1];
    grpc_linked_mdelem *l = (grpc_linked_mdelem *)&md->internal_data;
    GPR_ASSERT(sizeof(grpc_linked_mdelem) == sizeof(md->internal_data));
    l->md = grpc_mdelem_from_string_and_buffer(call->metadata_context, md->key,
                                               (const gpr_uint8 *)md->value,
                                               md->value_length);
    if (!grpc_mdstr_is_legal_header(l->md->key)) {
      gpr_log(GPR_ERROR, "attempt to send invalid metadata key: %s",
              grpc_mdstr_as_c_string(l->md->key));
      return 0;
    } else if (!grpc_mdstr_is_bin_suffixed(l->md->key) &&
               !grpc_mdstr_is_legal_nonbin_header(l->md->value)) {
      gpr_log(GPR_ERROR, "attempt to send invalid metadata value");
      return 0;
    }
    l->next = next_md ? (grpc_linked_mdelem *)&next_md->internal_data : NULL;
    l->prev = prev_md ? (grpc_linked_mdelem *)&prev_md->internal_data : NULL;
  }
  return 1;
}

static grpc_mdelem_list chain_metadata_from_app(grpc_call *call, size_t count,
                                                grpc_metadata *metadata) {
  grpc_mdelem_list out;
  if (count == 0) {
    out.head = out.tail = NULL;
    return out;
  }
  out.head = (grpc_linked_mdelem *)&(metadata[0].internal_data);
  out.tail = (grpc_linked_mdelem *)&(metadata[count - 1].internal_data);
  return out;
}

/* Copy the contents of a byte buffer into stream ops */
static void copy_byte_buffer_to_stream_ops(grpc_byte_buffer *byte_buffer,
                                           grpc_stream_op_buffer *sopb) {
  size_t i;

  switch (byte_buffer->type) {
    case GRPC_BB_RAW:
      for (i = 0; i < byte_buffer->data.raw.slice_buffer.count; i++) {
        gpr_slice slice = byte_buffer->data.raw.slice_buffer.slices[i];
        gpr_slice_ref(slice);
        grpc_sopb_add_slice(sopb, slice);
      }
      break;
  }
}

static int fill_send_ops(grpc_call *call, grpc_transport_stream_op *op) {
  grpc_ioreq_data data;
  gpr_uint32 flags;
  grpc_metadata_batch mdb;
  size_t i;
  GPR_ASSERT(op->send_ops == NULL);

  switch (call->write_state) {
    case WRITE_STATE_INITIAL:
      if (!is_op_live(call, GRPC_IOREQ_SEND_INITIAL_METADATA)) {
        break;
      }
      data = call->request_data[GRPC_IOREQ_SEND_INITIAL_METADATA];
      mdb.list = chain_metadata_from_app(call, data.send_metadata.count,
                                         data.send_metadata.metadata);
      mdb.garbage.head = mdb.garbage.tail = NULL;
      mdb.deadline = call->send_deadline;
      for (i = 0; i < call->send_initial_metadata_count; i++) {
        grpc_metadata_batch_link_head(&mdb, &call->send_initial_metadata[i]);
      }
      grpc_sopb_add_metadata(&call->send_ops, mdb);
      op->send_ops = &call->send_ops;
      call->last_send_contains |= 1 << GRPC_IOREQ_SEND_INITIAL_METADATA;
      call->send_initial_metadata_count = 0;
    /* fall through intended */
    case WRITE_STATE_STARTED:
      if (is_op_live(call, GRPC_IOREQ_SEND_MESSAGE)) {
        size_t length;
        data = call->request_data[GRPC_IOREQ_SEND_MESSAGE];
        flags = call->request_flags[GRPC_IOREQ_SEND_MESSAGE];
        length = grpc_byte_buffer_length(data.send_message);
        GPR_ASSERT(length <= GPR_UINT32_MAX);
        grpc_sopb_add_begin_message(&call->send_ops, (gpr_uint32)length, flags);
        copy_byte_buffer_to_stream_ops(data.send_message, &call->send_ops);
        op->send_ops = &call->send_ops;
        call->last_send_contains |= 1 << GRPC_IOREQ_SEND_MESSAGE;
      }
      if (is_op_live(call, GRPC_IOREQ_SEND_CLOSE)) {
        op->is_last_send = 1;
        op->send_ops = &call->send_ops;
        call->last_send_contains |= 1 << GRPC_IOREQ_SEND_CLOSE;
        if (!call->is_client) {
          /* send trailing metadata */
          data = call->request_data[GRPC_IOREQ_SEND_TRAILING_METADATA];
          mdb.list = chain_metadata_from_app(call, data.send_metadata.count,
                                             data.send_metadata.metadata);
          mdb.garbage.head = mdb.garbage.tail = NULL;
          mdb.deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
          /* send status */
          /* TODO(ctiller): cache common status values */
          data = call->request_data[GRPC_IOREQ_SEND_STATUS];
          grpc_metadata_batch_add_tail(
              &mdb, &call->status_link,
              grpc_channel_get_reffed_status_elem(call->channel,
                                                  data.send_status.code));
          if (data.send_status.details) {
            grpc_metadata_batch_add_tail(
                &mdb, &call->details_link,
                grpc_mdelem_from_metadata_strings(
                    call->metadata_context,
                    GRPC_MDSTR_REF(
                        grpc_channel_get_message_string(call->channel)),
                    data.send_status.details));
            call->request_data[GRPC_IOREQ_SEND_STATUS].send_status.details =
                NULL;
          }
          grpc_sopb_add_metadata(&call->send_ops, mdb);
        }
      }
      break;
    case WRITE_STATE_WRITE_CLOSED:
      break;
  }
  if (op->send_ops) {
    op->on_done_send = &call->on_done_send;
  }
  return op->send_ops != NULL;
}

static grpc_call_error start_ioreq_error(grpc_call *call,
                                         gpr_uint32 mutated_ops,
                                         grpc_call_error ret) {
  size_t i;
  for (i = 0; i < GRPC_IOREQ_OP_COUNT; i++) {
    if (mutated_ops & (1u << i)) {
      call->request_set[i] = REQSET_EMPTY;
    }
  }
  return ret;
}

static void finish_read_ops(grpc_call *call) {
  int empty;

  if (is_op_live(call, GRPC_IOREQ_RECV_MESSAGE)) {
    empty =
        (NULL == (*call->request_data[GRPC_IOREQ_RECV_MESSAGE].recv_message =
                      grpc_bbq_pop(&call->incoming_queue)));
    if (!empty) {
      finish_live_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, 1);
      empty = grpc_bbq_empty(&call->incoming_queue);
    }
  } else {
    empty = grpc_bbq_empty(&call->incoming_queue);
  }

  switch (call->read_state) {
    case READ_STATE_STREAM_CLOSED:
      if (empty && !call->have_alarm) {
        finish_ioreq_op(call, GRPC_IOREQ_RECV_CLOSE, 1);
      }
    /* fallthrough */
    case READ_STATE_READ_CLOSED:
      if (empty) {
        finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, 1);
      }
      finish_ioreq_op(call, GRPC_IOREQ_RECV_STATUS, 1);
      finish_ioreq_op(call, GRPC_IOREQ_RECV_STATUS_DETAILS, 1);
      finish_ioreq_op(call, GRPC_IOREQ_RECV_TRAILING_METADATA, 1);
    /* fallthrough */
    case READ_STATE_GOT_INITIAL_METADATA:
      finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, 1);
    /* fallthrough */
    case READ_STATE_INITIAL:
      /* do nothing */
      break;
  }
}

static grpc_call_error start_ioreq(grpc_call *call, const grpc_ioreq *reqs,
                                   size_t nreqs,
                                   grpc_ioreq_completion_func completion,
                                   void *user_data) {
  size_t i;
  gpr_uint16 have_ops = 0;
  grpc_ioreq_op op;
  reqinfo_master *master;
  grpc_ioreq_data data;
  gpr_uint8 set;

  if (nreqs == 0) {
    return GRPC_CALL_OK;
  }

  set = reqs[0].op;

  for (i = 0; i < nreqs; i++) {
    op = reqs[i].op;
    if (call->request_set[op] < GRPC_IOREQ_OP_COUNT) {
      return start_ioreq_error(call, have_ops,
                               GRPC_CALL_ERROR_TOO_MANY_OPERATIONS);
    } else if (call->request_set[op] == REQSET_DONE) {
      return start_ioreq_error(call, have_ops, GRPC_CALL_ERROR_ALREADY_INVOKED);
    }
    data = reqs[i].data;
    if (op == GRPC_IOREQ_SEND_INITIAL_METADATA ||
        op == GRPC_IOREQ_SEND_TRAILING_METADATA) {
      if (!prepare_application_metadata(call, data.send_metadata.count,
                                        data.send_metadata.metadata)) {
        return start_ioreq_error(call, have_ops,
                                 GRPC_CALL_ERROR_INVALID_METADATA);
      }
    }
    if (op == GRPC_IOREQ_SEND_STATUS) {
      set_status_code(call, STATUS_FROM_SERVER_STATUS,
                      (gpr_uint32)reqs[i].data.send_status.code);
      if (reqs[i].data.send_status.details) {
        set_status_details(call, STATUS_FROM_SERVER_STATUS,
                           GRPC_MDSTR_REF(reqs[i].data.send_status.details));
      }
    }
    /* NB: the following integer arithmetic operation needs to be in its
     * expanded form due to the "integral promotion" performed (see section
     * 3.2.1.1 of the C89 draft standard). A cast to the smaller container type
     * is then required to avoid the compiler warning */
    have_ops = (gpr_uint16)(have_ops | (1u << op));

    call->request_data[op] = data;
    call->request_flags[op] = reqs[i].flags;
    call->request_set[op] = set;
  }

  master = &call->masters[set];
  master->success = 1;
  master->need_mask = have_ops;
  master->complete_mask = 0;
  master->on_complete = completion;
  master->user_data = user_data;

  finish_read_ops(call);
  early_out_write_ops(call);

  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_start_ioreq_and_call_back(
    grpc_exec_ctx *exec_ctx, grpc_call *call, const grpc_ioreq *reqs,
    size_t nreqs, grpc_ioreq_completion_func on_complete, void *user_data) {
  grpc_call_error err;
  lock(call);
  err = start_ioreq(call, reqs, nreqs, on_complete, user_data);
  unlock(exec_ctx, call);
  return err;
}

void grpc_call_destroy(grpc_call *c) {
  int cancel;
  grpc_call *parent = c->parent;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE("grpc_call_destroy(c=%p)", 1, (c));

  if (parent) {
    gpr_mu_lock(&parent->mu);
    if (c == parent->first_child) {
      parent->first_child = c->sibling_next;
      if (c == parent->first_child) {
        parent->first_child = NULL;
      }
      c->sibling_prev->sibling_next = c->sibling_next;
      c->sibling_next->sibling_prev = c->sibling_prev;
    }
    gpr_mu_unlock(&parent->mu);
    GRPC_CALL_INTERNAL_UNREF(&exec_ctx, parent, "child");
  }

  lock(c);
  GPR_ASSERT(!c->destroy_called);
  c->destroy_called = 1;
  if (c->have_alarm) {
    grpc_alarm_cancel(&exec_ctx, &c->alarm);
  }
  cancel = c->read_state != READ_STATE_STREAM_CLOSED;
  unlock(&exec_ctx, c);
  if (cancel) grpc_call_cancel(c, NULL);
  GRPC_CALL_INTERNAL_UNREF(&exec_ctx, c, "destroy");
  grpc_exec_ctx_finish(&exec_ctx);
}

grpc_call_error grpc_call_cancel(grpc_call *call, void *reserved) {
  GRPC_API_TRACE("grpc_call_cancel(call=%p, reserved=%p)", 2, (call, reserved));
  GPR_ASSERT(!reserved);
  return grpc_call_cancel_with_status(call, GRPC_STATUS_CANCELLED, "Cancelled",
                                      NULL);
}

grpc_call_error grpc_call_cancel_with_status(grpc_call *c,
                                             grpc_status_code status,
                                             const char *description,
                                             void *reserved) {
  grpc_call_error r;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_API_TRACE(
      "grpc_call_cancel_with_status("
      "c=%p, status=%d, description=%s, reserved=%p)",
      4, (c, (int)status, description, reserved));
  GPR_ASSERT(reserved == NULL);
  lock(c);
  r = cancel_with_status(c, status, description);
  unlock(&exec_ctx, c);
  grpc_exec_ctx_finish(&exec_ctx);
  return r;
}

static grpc_call_error cancel_with_status(grpc_call *c, grpc_status_code status,
                                          const char *description) {
  grpc_mdstr *details =
      description ? grpc_mdstr_from_string(c->metadata_context, description)
                  : NULL;

  GPR_ASSERT(status != GRPC_STATUS_OK);

  set_status_code(c, STATUS_FROM_API_OVERRIDE, (gpr_uint32)status);
  set_status_details(c, STATUS_FROM_API_OVERRIDE, details);

  c->cancel_with_status = status;

  return GRPC_CALL_OK;
}

static void finished_loose_op(grpc_exec_ctx *exec_ctx, void *call,
                              int success_ignored) {
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "loose-op");
}

typedef struct {
  grpc_call *call;
  grpc_closure closure;
} finished_loose_op_allocated_args;

static void finished_loose_op_allocated(grpc_exec_ctx *exec_ctx, void *alloc,
                                        int success) {
  finished_loose_op_allocated_args *args = alloc;
  finished_loose_op(exec_ctx, args->call, success);
  gpr_free(args);
}

static void execute_op(grpc_exec_ctx *exec_ctx, grpc_call *call,
                       grpc_transport_stream_op *op) {
  grpc_call_element *elem;

  GPR_ASSERT(op->on_consumed == NULL);
  if (op->cancel_with_status != GRPC_STATUS_OK || op->bind_pollset) {
    GRPC_CALL_INTERNAL_REF(call, "loose-op");
    if (op->bind_pollset) {
      op->on_consumed = &call->on_done_bind;
    } else {
      finished_loose_op_allocated_args *args = gpr_malloc(sizeof(*args));
      args->call = call;
      grpc_closure_init(&args->closure, finished_loose_op_allocated, args);
      op->on_consumed = &args->closure;
    }
  }

  elem = CALL_ELEM_FROM_CALL(call, 0);
  op->context = call->context;
  elem->filter->start_transport_stream_op(exec_ctx, elem, op);
}

char *grpc_call_get_peer(grpc_call *call) {
  grpc_call_element *elem = CALL_ELEM_FROM_CALL(call, 0);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  char *result = elem->filter->get_peer(&exec_ctx, elem);
  GRPC_API_TRACE("grpc_call_get_peer(%p)", 1, (call));
  grpc_exec_ctx_finish(&exec_ctx);
  return result;
}

grpc_call *grpc_call_from_top_element(grpc_call_element *elem) {
  return CALL_FROM_TOP_ELEM(elem);
}

static void call_alarm(grpc_exec_ctx *exec_ctx, void *arg, int success) {
  grpc_call *call = arg;
  lock(call);
  call->have_alarm = 0;
  if (success) {
    cancel_with_status(call, GRPC_STATUS_DEADLINE_EXCEEDED,
                       "Deadline Exceeded");
  }
  finish_read_ops(call);
  unlock(exec_ctx, call);
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "alarm");
}

static void set_deadline_alarm(grpc_exec_ctx *exec_ctx, grpc_call *call,
                               gpr_timespec deadline) {
  if (call->have_alarm) {
    gpr_log(GPR_ERROR, "Attempt to set deadline alarm twice");
    assert(0);
    return;
  }
  GRPC_CALL_INTERNAL_REF(call, "alarm");
  call->have_alarm = 1;
  call->send_deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);
  grpc_alarm_init(exec_ctx, &call->alarm, call->send_deadline, call_alarm, call,
                  gpr_now(GPR_CLOCK_MONOTONIC));
}

/* we offset status by a small amount when storing it into transport metadata
   as metadata cannot store a 0 value (which is used as OK for grpc_status_codes
   */
#define STATUS_OFFSET 1
static void destroy_status(void *ignored) {}

static gpr_uint32 decode_status(grpc_mdelem *md) {
  gpr_uint32 status;
  void *user_data = grpc_mdelem_get_user_data(md, destroy_status);
  if (user_data) {
    status = ((gpr_uint32)(gpr_intptr)user_data) - STATUS_OFFSET;
  } else {
    if (!gpr_parse_bytes_to_uint32(grpc_mdstr_as_c_string(md->value),
                                   GPR_SLICE_LENGTH(md->value->slice),
                                   &status)) {
      status = GRPC_STATUS_UNKNOWN; /* could not parse status code */
    }
    grpc_mdelem_set_user_data(md, destroy_status,
                              (void *)(gpr_intptr)(status + STATUS_OFFSET));
  }
  return status;
}

/* just as for status above, we need to offset: metadata userdata can't hold a
 * zero (null), which in this case is used to signal no compression */
#define COMPRESS_OFFSET 1
static void destroy_compression(void *ignored) {}

static gpr_uint32 decode_compression(grpc_mdelem *md) {
  grpc_compression_algorithm algorithm;
  void *user_data = grpc_mdelem_get_user_data(md, destroy_compression);
  if (user_data) {
    algorithm =
        ((grpc_compression_algorithm)(gpr_intptr)user_data) - COMPRESS_OFFSET;
  } else {
    const char *md_c_str = grpc_mdstr_as_c_string(md->value);
    if (!grpc_compression_algorithm_parse(md_c_str, strlen(md_c_str),
                                          &algorithm)) {
      gpr_log(GPR_ERROR, "Invalid compression algorithm: '%s'", md_c_str);
      assert(0);
    }
    grpc_mdelem_set_user_data(
        md, destroy_compression,
        (void *)(gpr_intptr)(algorithm + COMPRESS_OFFSET));
  }
  return algorithm;
}

static void recv_metadata(grpc_exec_ctx *exec_ctx, grpc_call *call,
                          grpc_metadata_batch *md) {
  grpc_linked_mdelem *l;
  grpc_metadata_array *dest;
  grpc_metadata *mdusr;
  int is_trailing;
  grpc_mdctx *mdctx = call->metadata_context;

  is_trailing = call->read_state >= READ_STATE_GOT_INITIAL_METADATA;
  for (l = md->list.head; l != NULL; l = l->next) {
    grpc_mdelem *mdel = l->md;
    grpc_mdstr *key = mdel->key;
    if (key == grpc_channel_get_status_string(call->channel)) {
      GPR_TIMER_BEGIN("status", 0);
      set_status_code(call, STATUS_FROM_WIRE, decode_status(mdel));
      GPR_TIMER_END("status", 0);
    } else if (key == grpc_channel_get_message_string(call->channel)) {
      GPR_TIMER_BEGIN("status-details", 0);
      set_status_details(call, STATUS_FROM_WIRE, GRPC_MDSTR_REF(mdel->value));
      GPR_TIMER_END("status-details", 0);
    } else if (key ==
               grpc_channel_get_compression_algorithm_string(call->channel)) {
      GPR_TIMER_BEGIN("compression_algorithm", 0);
      set_compression_algorithm(call, decode_compression(mdel));
      GPR_TIMER_END("compression_algorithm", 0);
    } else if (key == grpc_channel_get_encodings_accepted_by_peer_string(
                          call->channel)) {
      GPR_TIMER_BEGIN("encodings_accepted_by_peer", 0);
      set_encodings_accepted_by_peer(call, mdel->value->slice);
      GPR_TIMER_END("encodings_accepted_by_peer", 0);
    } else {
      GPR_TIMER_BEGIN("report_up", 0);
      dest = &call->buffered_metadata[is_trailing];
      if (dest->count == dest->capacity) {
        dest->capacity = GPR_MAX(dest->capacity + 8, dest->capacity * 2);
        dest->metadata =
            gpr_realloc(dest->metadata, sizeof(grpc_metadata) * dest->capacity);
      }
      mdusr = &dest->metadata[dest->count++];
      mdusr->key = grpc_mdstr_as_c_string(mdel->key);
      mdusr->value = grpc_mdstr_as_c_string(mdel->value);
      mdusr->value_length = GPR_SLICE_LENGTH(mdel->value->slice);
      if (call->owned_metadata_count == call->owned_metadata_capacity) {
        call->owned_metadata_capacity =
            GPR_MAX(call->owned_metadata_capacity + 8,
                    call->owned_metadata_capacity * 2);
        call->owned_metadata =
            gpr_realloc(call->owned_metadata,
                        sizeof(grpc_mdelem *) * call->owned_metadata_capacity);
      }
      call->owned_metadata[call->owned_metadata_count++] = mdel;
      l->md = NULL;
      GPR_TIMER_END("report_up", 0);
    }
  }
  if (gpr_time_cmp(md->deadline, gpr_inf_future(md->deadline.clock_type)) !=
          0 &&
      !call->is_client) {
    GPR_TIMER_BEGIN("set_deadline_alarm", 0);
    set_deadline_alarm(exec_ctx, call, md->deadline);
    GPR_TIMER_END("set_deadline_alarm", 0);
  }
  if (!is_trailing) {
    call->read_state = READ_STATE_GOT_INITIAL_METADATA;
  }

  grpc_mdctx_lock(mdctx);
  for (l = md->list.head; l; l = l->next) {
    if (l->md) GRPC_MDCTX_LOCKED_MDELEM_UNREF(mdctx, l->md);
  }
  for (l = md->garbage.head; l; l = l->next) {
    GRPC_MDCTX_LOCKED_MDELEM_UNREF(mdctx, l->md);
  }
  grpc_mdctx_unlock(mdctx);
}

grpc_call_stack *grpc_call_get_call_stack(grpc_call *call) {
  return CALL_STACK_FROM_CALL(call);
}

/*
 * BATCH API IMPLEMENTATION
 */

static void set_status_value_directly(grpc_status_code status, void *dest) {
  *(grpc_status_code *)dest = status;
}

static void set_cancelled_value(grpc_status_code status, void *dest) {
  *(grpc_status_code *)dest = (status != GRPC_STATUS_OK);
}

static void finish_batch(grpc_exec_ctx *exec_ctx, grpc_call *call, int success,
                         void *tag) {
  grpc_cq_end_op(exec_ctx, call->cq, tag, success, done_completion, call,
                 allocate_completion(call));
}

static void finish_batch_with_close(grpc_exec_ctx *exec_ctx, grpc_call *call,
                                    int success, void *tag) {
  grpc_cq_end_op(exec_ctx, call->cq, tag, 1, done_completion, call,
                 allocate_completion(call));
}

static int are_write_flags_valid(gpr_uint32 flags) {
  /* check that only bits in GRPC_WRITE_(INTERNAL?)_USED_MASK are set */
  const gpr_uint32 allowed_write_positions =
      (GRPC_WRITE_USED_MASK | GRPC_WRITE_INTERNAL_USED_MASK);
  const gpr_uint32 invalid_positions = ~allowed_write_positions;
  return !(flags & invalid_positions);
}

grpc_call_error grpc_call_start_batch(grpc_call *call, const grpc_op *ops,
                                      size_t nops, void *tag, void *reserved) {
  grpc_ioreq reqs[GRPC_IOREQ_OP_COUNT];
  size_t in;
  size_t out;
  const grpc_op *op;
  grpc_ioreq *req;
  void (*finish_func)(grpc_exec_ctx *, grpc_call *, int, void *) = finish_batch;
  grpc_call_error error;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GPR_TIMER_BEGIN("grpc_call_start_batch", 0);

  GRPC_API_TRACE(
      "grpc_call_start_batch(call=%p, ops=%p, nops=%lu, tag=%p, reserved=%p)",
      5, (call, ops, (unsigned long)nops, tag, reserved));

  if (reserved != NULL) {
    error = GRPC_CALL_ERROR;
    goto done;
  }

  GRPC_CALL_LOG_BATCH(GPR_INFO, call, ops, nops, tag);

  if (nops == 0) {
    grpc_cq_begin_op(call->cq);
    GRPC_CALL_INTERNAL_REF(call, "completion");
    grpc_cq_end_op(&exec_ctx, call->cq, tag, 1, done_completion, call,
                   allocate_completion(call));
    error = GRPC_CALL_OK;
    goto done;
  }

  /* rewrite batch ops into ioreq ops */
  for (in = 0, out = 0; in < nops; in++) {
    op = &ops[in];
    if (op->reserved != NULL) {
      error = GRPC_CALL_ERROR;
      goto done;
    }
    switch (op->op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done;
        }
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_SEND_INITIAL_METADATA;
        req->data.send_metadata.count = op->data.send_initial_metadata.count;
        req->data.send_metadata.metadata =
            op->data.send_initial_metadata.metadata;
        req->flags = op->flags;
        break;
      case GRPC_OP_SEND_MESSAGE:
        if (!are_write_flags_valid(op->flags)) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done;
        }
        if (op->data.send_message == NULL) {
          error = GRPC_CALL_ERROR_INVALID_MESSAGE;
          goto done;
        }
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_SEND_MESSAGE;
        req->data.send_message = op->data.send_message;
        req->flags = op->flags;
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done;
        }
        if (!call->is_client) {
          error = GRPC_CALL_ERROR_NOT_ON_SERVER;
          goto done;
        }
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_SEND_CLOSE;
        req->flags = op->flags;
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done;
        }
        if (call->is_client) {
          error = GRPC_CALL_ERROR_NOT_ON_CLIENT;
          goto done;
        }
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_SEND_TRAILING_METADATA;
        req->flags = op->flags;
        req->data.send_metadata.count =
            op->data.send_status_from_server.trailing_metadata_count;
        req->data.send_metadata.metadata =
            op->data.send_status_from_server.trailing_metadata;
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_SEND_STATUS;
        req->data.send_status.code = op->data.send_status_from_server.status;
        req->data.send_status.details =
            op->data.send_status_from_server.status_details != NULL
                ? grpc_mdstr_from_string(
                      call->metadata_context,
                      op->data.send_status_from_server.status_details)
                : NULL;
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_SEND_CLOSE;
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done;
        }
        if (!call->is_client) {
          error = GRPC_CALL_ERROR_NOT_ON_SERVER;
          goto done;
        }
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_RECV_INITIAL_METADATA;
        req->data.recv_metadata = op->data.recv_initial_metadata;
        req->data.recv_metadata->count = 0;
        req->flags = op->flags;
        break;
      case GRPC_OP_RECV_MESSAGE:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done;
        }
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_RECV_MESSAGE;
        req->data.recv_message = op->data.recv_message;
        req->flags = op->flags;
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done;
        }
        if (!call->is_client) {
          error = GRPC_CALL_ERROR_NOT_ON_SERVER;
          goto done;
        }
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_RECV_STATUS;
        req->flags = op->flags;
        req->data.recv_status.set_value = set_status_value_directly;
        req->data.recv_status.user_data = op->data.recv_status_on_client.status;
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_RECV_STATUS_DETAILS;
        req->data.recv_status_details.details =
            op->data.recv_status_on_client.status_details;
        req->data.recv_status_details.details_capacity =
            op->data.recv_status_on_client.status_details_capacity;
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_RECV_TRAILING_METADATA;
        req->data.recv_metadata =
            op->data.recv_status_on_client.trailing_metadata;
        req->data.recv_metadata->count = 0;
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_RECV_CLOSE;
        finish_func = finish_batch_with_close;
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done;
        }
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_RECV_STATUS;
        req->flags = op->flags;
        req->data.recv_status.set_value = set_cancelled_value;
        req->data.recv_status.user_data =
            op->data.recv_close_on_server.cancelled;
        req = &reqs[out++];
        if (out > GRPC_IOREQ_OP_COUNT) {
          error = GRPC_CALL_ERROR_BATCH_TOO_BIG;
          goto done;
        }
        req->op = GRPC_IOREQ_RECV_CLOSE;
        finish_func = finish_batch_with_close;
        break;
    }
  }

  GRPC_CALL_INTERNAL_REF(call, "completion");
  grpc_cq_begin_op(call->cq);

  error = grpc_call_start_ioreq_and_call_back(&exec_ctx, call, reqs, out,
                                              finish_func, tag);
done:
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_TIMER_END("grpc_call_start_batch", 0);
  return error;
}

void grpc_call_context_set(grpc_call *call, grpc_context_index elem,
                           void *value, void (*destroy)(void *value)) {
  if (call->context[elem].destroy) {
    call->context[elem].destroy(call->context[elem].value);
  }
  call->context[elem].value = value;
  call->context[elem].destroy = destroy;
}

void *grpc_call_context_get(grpc_call *call, grpc_context_index elem) {
  return call->context[elem].value;
}

gpr_uint8 grpc_call_is_client(grpc_call *call) { return call->is_client; }
