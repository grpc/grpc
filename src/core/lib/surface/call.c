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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/compression/algorithm_metadata.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/transport.h"

/** The maximum number of concurrent batches possible.
    Based upon the maximum number of individually queueable ops in the batch
    api:
      - initial metadata send
      - message send
      - status/close send (depending on client/server)
      - initial metadata recv
      - message recv
      - status/close recv (depending on client/server) */
#define MAX_CONCURRENT_BATCHES 6

#define MAX_SEND_EXTRA_METADATA_COUNT 3

/* Status data for a request can come from several sources; this
   enumerates them all, and acts as a priority sorting for which
   status to return to the application - earlier entries override
   later ones */
typedef enum {
  /* Status came from the application layer overriding whatever
     the wire says */
  STATUS_FROM_API_OVERRIDE = 0,
  /* Status came from 'the wire' - or somewhere below the surface
     layer */
  STATUS_FROM_WIRE,
  /* Status was created by some internal channel stack operation */
  STATUS_FROM_CORE,
  /* Status came from the server sending status */
  STATUS_FROM_SERVER_STATUS,
  STATUS_SOURCE_COUNT
} status_source;

typedef struct {
  bool is_set;
  grpc_error *error;
} received_status;

#define MAX_ERRORS_PER_BATCH 3

typedef struct batch_control {
  grpc_call *call;
  grpc_cq_completion cq_completion;
  grpc_closure finish_batch;
  void *notify_tag;
  gpr_refcount steps_to_complete;

  grpc_error *errors[MAX_ERRORS_PER_BATCH];
  gpr_atm num_errors;

  bool send_initial_metadata;
  bool send_message;
  bool send_final_op;
  bool recv_initial_metadata;
  bool recv_message;
  bool recv_final_op;
  bool is_notify_tag_closure;

  /* TODO(ctiller): now that this is inlined, figure out how much of the above
                    state can be eliminated */
  grpc_transport_stream_op op;
} batch_control;

typedef enum {
  SENDRECV_IDLE,
  SENDRECV_FULL_MESSAGE,
  SENDRECV_INCREMENTAL
} sendrecv_mode;

typedef enum {
  NEXT_SLICE_EMPTY,
  NEXT_SLICE_REQUESTED,
  NEXT_SLICE_PRESENT,
} next_slice_state;

struct grpc_call {
  grpc_completion_queue *cq;
  grpc_polling_entity pollent;
  grpc_channel *channel;
  grpc_call *parent;
  grpc_call *first_child;
  gpr_timespec start_time;
  /* TODO(ctiller): share with cq if possible? */
  gpr_mu mu;

  /* client or server call */
  bool is_client;
  /** has grpc_call_destroy been called */
  bool destroy_called;
  /** flag indicating that cancellation is inherited */
  bool cancellation_is_inherited;
  /** bitmask of live batches */
  uint8_t used_batches;
  /** which ops are in-flight */
  bool sent_initial_metadata;
  sendrecv_mode send_mode;
  bool sent_final_op;
  bool received_initial_metadata;
  sendrecv_mode recv_mode;
  bool requested_final_op;
  bool received_final_op;

  /* have we received initial metadata */
  bool has_initial_md_been_received;

  batch_control active_batches[MAX_CONCURRENT_BATCHES];

  /* first idx: is_receiving, second idx: is_trailing */
  grpc_metadata_batch metadata_batch[2][2];

  /* Buffered read metadata waiting to be returned to the application.
     Element 0 is initial metadata, element 1 is trailing metadata. */
  grpc_metadata_array *buffered_metadata[2];

  /* Received call statuses from various sources */
  received_status status[STATUS_SOURCE_COUNT];

  /* Call data useful used for reporting. Only valid after the call has
   * completed */
  grpc_call_final_info final_info;

  /* Compression algorithm for *incoming* data */
  grpc_compression_algorithm incoming_compression_algorithm;
  /* Supported encodings (compression algorithms), a bitset */
  uint32_t encodings_accepted_by_peer;

  /* Contexts for various subsystems (security, tracing, ...). */
  grpc_call_context_element context[GRPC_CONTEXT_COUNT];

  /* for the client, extra metadata is initial metadata; for the
     server, it's trailing metadata */
  grpc_linked_mdelem send_extra_metadata[MAX_SEND_EXTRA_METADATA_COUNT];
  int send_extra_metadata_count;
  gpr_timespec send_deadline;

  /** siblings: children of the same parent form a list, and this list is
     protected under
      parent->mu */
  grpc_call *sibling_next;
  grpc_call *sibling_prev;

  union {
    struct {
      grpc_slice_buffer_stream stream;
    } full;
    struct {
      grpc_byte_stream stream;
      grpc_byte_buffer *buffer;
      union {
        struct {
          size_t slice_index;
        } raw;
        struct {
          size_t elem_index;
          gpr_refcount waiting_refs;
          grpc_slice_refcount slice_refcount;
        } iovec;
      } buffer_progress;
      void *tag;
      grpc_closure *on_next;
      bool next_is_buffer;
      grpc_cq_completion cq_completion;
      batch_control *bctl;
      uint32_t awaiting_bytes;
      grpc_slice *next_slice;
    } incremental;
  } sending;
  grpc_byte_stream *receiving_stream;
  union {
    struct {
      grpc_byte_buffer **buffer;
      grpc_slice slice;
    } full;
    struct {
      uint32_t *length_target;
      uint32_t remaining;
      grpc_byte_buffer *pull_target;
      void *pull_tag;
      union {
        struct {
          next_slice_state next_slice_state;
        } raw;
        struct {
          size_t pull_idx;
        } iovec;
      } buffer_progress;
      grpc_slice next_slice;
      grpc_cq_completion cq_completion;
    } incremental;
  } receiving;
  grpc_closure receiving_next_step;
  grpc_closure receiving_initial_metadata_ready;
  uint32_t test_only_last_message_flags;

  union {
    struct {
      grpc_status_code *status;
      grpc_slice *status_details;
    } client;
    struct {
      int *cancelled;
    } server;
  } final_op;

  void *saved_receiving_stream_ready_bctlp;
};

#define CALL_STACK_FROM_CALL(call) ((grpc_call_stack *)((call) + 1))
#define CALL_FROM_CALL_STACK(call_stack) (((grpc_call *)(call_stack)) - 1)
#define CALL_ELEM_FROM_CALL(call, idx) \
  grpc_call_stack_element(CALL_STACK_FROM_CALL(call), idx)
#define CALL_FROM_TOP_ELEM(top_elem) \
  CALL_FROM_CALL_STACK(grpc_call_stack_from_top_element(top_elem))

static void execute_op(grpc_exec_ctx *exec_ctx, grpc_call *call,
                       grpc_transport_stream_op *op);
static grpc_call_error cancel_with_status(grpc_exec_ctx *exec_ctx, grpc_call *c,
                                          grpc_status_code status,
                                          const char *description);
static void cancel_with_error(grpc_exec_ctx *exec_ctx, grpc_call *c,
                              grpc_error *error);
static void destroy_call(grpc_exec_ctx *exec_ctx, void *call_stack,
                         grpc_error *error);
static void receiving_slice_ready(grpc_exec_ctx *exec_ctx, void *bctlp,
                                  grpc_error *error);
static void get_final_status(grpc_call *call,
                             void (*set_value)(grpc_status_code code,
                                               void *user_data),
                             void *set_value_user_data, grpc_slice *details);
static void set_status_value_directly(grpc_status_code status, void *dest);
static void set_status_from_error(grpc_exec_ctx *exec_ctx, grpc_call *call,
                                  status_source source, grpc_error *error);
static void process_data_after_md(grpc_exec_ctx *exec_ctx, batch_control *bctl);
static void process_incremental_data_after_md(grpc_exec_ctx *exec_ctx,
                                              batch_control *bctl);
static void post_batch_completion(grpc_exec_ctx *exec_ctx, batch_control *bctl);
static void add_batch_error(grpc_exec_ctx *exec_ctx, batch_control *bctl,
                            grpc_error *error);
static void finish_batch_step(grpc_exec_ctx *exec_ctx, batch_control *bctl);

/*******************************************************************************
 * OVERALL CALL CONSTRUCTION/DESTRUCTION
 */

grpc_error *grpc_call_create(grpc_exec_ctx *exec_ctx,
                             const grpc_call_create_args *args,
                             grpc_call **out_call) {
  size_t i, j;
  grpc_channel_stack *channel_stack =
      grpc_channel_get_channel_stack(args->channel);
  grpc_call *call;
  GPR_TIMER_BEGIN("grpc_call_create", 0);
  call = gpr_malloc(sizeof(grpc_call) + channel_stack->call_stack_size);
  *out_call = call;
  memset(call, 0, sizeof(grpc_call));
  gpr_mu_init(&call->mu);
  call->channel = args->channel;
  call->cq = args->cq;
  call->parent = args->parent_call;
  call->start_time = gpr_now(GPR_CLOCK_MONOTONIC);
  /* Always support no compression */
  GPR_BITSET(&call->encodings_accepted_by_peer, GRPC_COMPRESS_NONE);
  call->is_client = args->server_transport_data == NULL;
  grpc_slice path = grpc_empty_slice();
  if (call->is_client) {
    GPR_ASSERT(args->add_initial_metadata_count <
               MAX_SEND_EXTRA_METADATA_COUNT);
    for (i = 0; i < args->add_initial_metadata_count; i++) {
      call->send_extra_metadata[i].md = args->add_initial_metadata[i];
      if (grpc_slice_eq(GRPC_MDKEY(args->add_initial_metadata[i]),
                        GRPC_MDSTR_PATH)) {
        path = grpc_slice_ref_internal(
            GRPC_MDVALUE(args->add_initial_metadata[i]));
      }
    }
    call->send_extra_metadata_count = (int)args->add_initial_metadata_count;
  } else {
    GPR_ASSERT(args->add_initial_metadata_count == 0);
    call->send_extra_metadata_count = 0;
  }
  for (i = 0; i < 2; i++) {
    for (j = 0; j < 2; j++) {
      call->metadata_batch[i][j].deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
    }
  }
  gpr_timespec send_deadline =
      gpr_convert_clock_type(args->send_deadline, GPR_CLOCK_MONOTONIC);

  if (args->parent_call != NULL) {
    GRPC_CALL_INTERNAL_REF(args->parent_call, "child");
    GPR_ASSERT(call->is_client);
    GPR_ASSERT(!args->parent_call->is_client);

    gpr_mu_lock(&args->parent_call->mu);

    if (args->propagation_mask & GRPC_PROPAGATE_DEADLINE) {
      send_deadline = gpr_time_min(
          gpr_convert_clock_type(send_deadline,
                                 args->parent_call->send_deadline.clock_type),
          args->parent_call->send_deadline);
    }
    /* for now GRPC_PROPAGATE_TRACING_CONTEXT *MUST* be passed with
     * GRPC_PROPAGATE_STATS_CONTEXT */
    /* TODO(ctiller): This should change to use the appropriate census start_op
     * call. */
    if (args->propagation_mask & GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT) {
      GPR_ASSERT(args->propagation_mask & GRPC_PROPAGATE_CENSUS_STATS_CONTEXT);
      grpc_call_context_set(
          call, GRPC_CONTEXT_TRACING,
          args->parent_call->context[GRPC_CONTEXT_TRACING].value, NULL);
    } else {
      GPR_ASSERT(args->propagation_mask & GRPC_PROPAGATE_CENSUS_STATS_CONTEXT);
    }
    if (args->propagation_mask & GRPC_PROPAGATE_CANCELLATION) {
      call->cancellation_is_inherited = 1;
    }

    if (args->parent_call->first_child == NULL) {
      args->parent_call->first_child = call;
      call->sibling_next = call->sibling_prev = call;
    } else {
      call->sibling_next = args->parent_call->first_child;
      call->sibling_prev = args->parent_call->first_child->sibling_prev;
      call->sibling_next->sibling_prev = call->sibling_prev->sibling_next =
          call;
    }

    gpr_mu_unlock(&args->parent_call->mu);
  }

  call->send_deadline = send_deadline;

  GRPC_CHANNEL_INTERNAL_REF(args->channel, "call");
  /* initial refcount dropped by grpc_call_destroy */
  grpc_error *error = grpc_call_stack_init(
      exec_ctx, channel_stack, 1, destroy_call, call, call->context,
      args->server_transport_data, path, call->start_time, send_deadline,
      CALL_STACK_FROM_CALL(call));
  if (error != GRPC_ERROR_NONE) {
    cancel_with_error(exec_ctx, call, GRPC_ERROR_REF(error));
  }
  if (args->cq != NULL) {
    GPR_ASSERT(
        args->pollset_set_alternative == NULL &&
        "Only one of 'cq' and 'pollset_set_alternative' should be non-NULL.");
    GRPC_CQ_INTERNAL_REF(args->cq, "bind");
    call->pollent =
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(args->cq));
  }
  if (args->pollset_set_alternative != NULL) {
    call->pollent = grpc_polling_entity_create_from_pollset_set(
        args->pollset_set_alternative);
  }
  if (!grpc_polling_entity_is_empty(&call->pollent)) {
    grpc_call_stack_set_pollset_or_pollset_set(
        exec_ctx, CALL_STACK_FROM_CALL(call), &call->pollent);
  }

  grpc_slice_unref_internal(exec_ctx, path);

  GPR_TIMER_END("grpc_call_create", 0);
  return error;
}

uint8_t grpc_call_is_client(grpc_call *call) { return call->is_client; }

void grpc_call_set_completion_queue(grpc_exec_ctx *exec_ctx, grpc_call *call,
                                    grpc_completion_queue *cq) {
  GPR_ASSERT(cq);

  if (grpc_polling_entity_pollset_set(&call->pollent) != NULL) {
    gpr_log(GPR_ERROR, "A pollset_set is already registered for this call.");
    abort();
  }
  call->cq = cq;
  GRPC_CQ_INTERNAL_REF(cq, "bind");
  call->pollent = grpc_polling_entity_create_from_pollset(grpc_cq_pollset(cq));
  grpc_call_stack_set_pollset_or_pollset_set(
      exec_ctx, CALL_STACK_FROM_CALL(call), &call->pollent);
}

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define REF_REASON reason
#define REF_ARG , const char *reason
#else
#define REF_REASON ""
#define REF_ARG
#endif
void grpc_call_internal_ref(grpc_call *c REF_ARG) {
  GRPC_CALL_STACK_REF(CALL_STACK_FROM_CALL(c), REF_REASON);
}
void grpc_call_internal_unref(grpc_exec_ctx *exec_ctx, grpc_call *c REF_ARG) {
  GRPC_CALL_STACK_UNREF(exec_ctx, CALL_STACK_FROM_CALL(c), REF_REASON);
}

static void destroy_call(grpc_exec_ctx *exec_ctx, void *call,
                         grpc_error *error) {
  size_t i;
  int ii;
  grpc_call *c = call;
  GPR_TIMER_BEGIN("destroy_call", 0);
  for (i = 0; i < 2; i++) {
    grpc_metadata_batch_destroy(
        exec_ctx, &c->metadata_batch[1 /* is_receiving */][i /* is_initial */]);
  }
  if (c->receiving_stream != NULL) {
    grpc_byte_stream_destroy(exec_ctx, c->receiving_stream);
  }
  gpr_mu_destroy(&c->mu);
  for (ii = 0; ii < c->send_extra_metadata_count; ii++) {
    GRPC_MDELEM_UNREF(exec_ctx, c->send_extra_metadata[ii].md);
  }
  for (i = 0; i < GRPC_CONTEXT_COUNT; i++) {
    if (c->context[i].destroy) {
      c->context[i].destroy(c->context[i].value);
    }
  }
  if (c->cq) {
    GRPC_CQ_INTERNAL_UNREF(c->cq, "bind");
  }
  grpc_channel *channel = c->channel;

  get_final_status(call, set_status_value_directly, &c->final_info.final_status,
                   NULL);
  c->final_info.stats.latency =
      gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), c->start_time);

  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    GRPC_ERROR_UNREF(c->status[i].error);
  }

  grpc_call_stack_destroy(exec_ctx, CALL_STACK_FROM_CALL(c), &c->final_info, c);
  GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, channel, "call");
  GPR_TIMER_END("destroy_call", 0);
}

void grpc_call_destroy(grpc_call *c) {
  int cancel;
  grpc_call *parent = c->parent;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GPR_TIMER_BEGIN("grpc_call_destroy", 0);
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

  gpr_mu_lock(&c->mu);
  GPR_ASSERT(!c->destroy_called);
  c->destroy_called = 1;
  cancel = !c->received_final_op;
  gpr_mu_unlock(&c->mu);
  if (cancel) grpc_call_cancel(c, NULL);
  GRPC_CALL_INTERNAL_UNREF(&exec_ctx, c, "destroy");
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_TIMER_END("grpc_call_destroy", 0);
}

grpc_call_error grpc_call_cancel(grpc_call *call, void *reserved) {
  GRPC_API_TRACE("grpc_call_cancel(call=%p, reserved=%p)", 2, (call, reserved));
  GPR_ASSERT(!reserved);
  return grpc_call_cancel_with_status(call, GRPC_STATUS_CANCELLED, "Cancelled",
                                      NULL);
}

static void execute_op(grpc_exec_ctx *exec_ctx, grpc_call *call,
                       grpc_transport_stream_op *op) {
  grpc_call_element *elem;

  GPR_TIMER_BEGIN("execute_op", 0);
  elem = CALL_ELEM_FROM_CALL(call, 0);
  op->context = call->context;
  elem->filter->start_transport_stream_op(exec_ctx, elem, op);
  GPR_TIMER_END("execute_op", 0);
}

char *grpc_call_get_peer(grpc_call *call) {
  grpc_call_element *elem = CALL_ELEM_FROM_CALL(call, 0);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  char *result;
  GRPC_API_TRACE("grpc_call_get_peer(%p)", 1, (call));
  result = elem->filter->get_peer(&exec_ctx, elem);
  if (result == NULL) {
    result = grpc_channel_get_target(call->channel);
  }
  if (result == NULL) {
    result = gpr_strdup("unknown");
  }
  grpc_exec_ctx_finish(&exec_ctx);
  return result;
}

grpc_call *grpc_call_from_top_element(grpc_call_element *elem) {
  return CALL_FROM_TOP_ELEM(elem);
}

/*******************************************************************************
 * CANCELLATION
 */

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
  gpr_mu_lock(&c->mu);
  r = cancel_with_status(&exec_ctx, c, status, description);
  gpr_mu_unlock(&c->mu);
  grpc_exec_ctx_finish(&exec_ctx);
  return r;
}

typedef struct termination_closure {
  grpc_closure closure;
  grpc_call *call;
  grpc_error *error;
  grpc_transport_stream_op op;
} termination_closure;

static void done_termination(grpc_exec_ctx *exec_ctx, void *tcp,
                             grpc_error *error) {
  termination_closure *tc = tcp;
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, tc->call, "termination");
  gpr_free(tc);
}

static void send_termination(grpc_exec_ctx *exec_ctx, void *tcp,
                             grpc_error *error) {
  termination_closure *tc = tcp;
  memset(&tc->op, 0, sizeof(tc->op));
  tc->op.cancel_error = tc->error;
  /* reuse closure to catch completion */
  grpc_closure_init(&tc->closure, done_termination, tc,
                    grpc_schedule_on_exec_ctx);
  tc->op.on_complete = &tc->closure;
  execute_op(exec_ctx, tc->call, &tc->op);
}

static grpc_call_error terminate_with_status(grpc_exec_ctx *exec_ctx,
                                             termination_closure *tc) {
  set_status_from_error(exec_ctx, tc->call, STATUS_FROM_API_OVERRIDE,
                        GRPC_ERROR_REF(tc->error));
  grpc_closure_init(&tc->closure, send_termination, tc,
                    grpc_schedule_on_exec_ctx);
  GRPC_CALL_INTERNAL_REF(tc->call, "termination");
  grpc_closure_sched(exec_ctx, &tc->closure, GRPC_ERROR_NONE);
  return GRPC_CALL_OK;
}

static grpc_call_error terminate_with_error(grpc_exec_ctx *exec_ctx,
                                            grpc_call *c, grpc_error *error) {
  termination_closure *tc = gpr_malloc(sizeof(*tc));
  memset(tc, 0, sizeof(*tc));
  tc->call = c;
  tc->error = error;
  return terminate_with_status(exec_ctx, tc);
}

static void cancel_with_error(grpc_exec_ctx *exec_ctx, grpc_call *c,
                              grpc_error *error) {
  terminate_with_error(exec_ctx, c, error);
}

static grpc_error *error_from_status(grpc_status_code status,
                                     const char *description) {
  return grpc_error_set_int(
      grpc_error_set_str(GRPC_ERROR_CREATE(description),
                         GRPC_ERROR_STR_GRPC_MESSAGE, description),
      GRPC_ERROR_INT_GRPC_STATUS, status);
}

static grpc_call_error cancel_with_status(grpc_exec_ctx *exec_ctx, grpc_call *c,
                                          grpc_status_code status,
                                          const char *description) {
  return terminate_with_error(exec_ctx, c,
                              error_from_status(status, description));
}

/*******************************************************************************
 * FINAL STATUS CODE MANIPULATION
 */

static void get_final_status_from(grpc_call *call, status_source from_source,
                                  void (*set_value)(grpc_status_code code,
                                                    void *user_data),
                                  void *set_value_user_data,
                                  grpc_slice *details) {
  grpc_status_code code;
  const char *msg = NULL;
  grpc_error_get_status(call->status[from_source].error, call->send_deadline,
                        &code, &msg, NULL);

  set_value(code, set_value_user_data);
  if (details != NULL) {
    *details =
        msg == NULL ? grpc_empty_slice() : grpc_slice_from_copied_string(msg);
  }
}

static void get_final_status(grpc_call *call,
                             void (*set_value)(grpc_status_code code,
                                               void *user_data),
                             void *set_value_user_data, grpc_slice *details) {
  int i;
  /* search for the best status we can present: ideally the error we use has a
     clearly defined grpc-status, and we'll prefer that. */
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (call->status[i].is_set &&
        grpc_error_has_clear_grpc_status(call->status[i].error)) {
      get_final_status_from(call, (status_source)i, set_value,
                            set_value_user_data, details);
      return;
    }
  }
  /* If no clearly defined status exists, search for 'anything' */
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (call->status[i].is_set) {
      get_final_status_from(call, (status_source)i, set_value,
                            set_value_user_data, details);
      return;
    }
  }
  /* If nothing exists, set some default */
  if (call->is_client) {
    set_value(GRPC_STATUS_UNKNOWN, set_value_user_data);
  } else {
    set_value(GRPC_STATUS_OK, set_value_user_data);
  }
}

static void set_status_from_error(grpc_exec_ctx *exec_ctx, grpc_call *call,
                                  status_source source, grpc_error *error) {
  if (call->status[source].is_set) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  call->status[source].is_set = true;
  call->status[source].error = error;
}

/*******************************************************************************
 * COMPRESSION
 */

static void set_incoming_compression_algorithm(
    grpc_call *call, grpc_compression_algorithm algo) {
  GPR_ASSERT(algo < GRPC_COMPRESS_ALGORITHMS_COUNT);
  call->incoming_compression_algorithm = algo;
}

grpc_compression_algorithm grpc_call_test_only_get_compression_algorithm(
    grpc_call *call) {
  grpc_compression_algorithm algorithm;
  gpr_mu_lock(&call->mu);
  algorithm = call->incoming_compression_algorithm;
  gpr_mu_unlock(&call->mu);
  return algorithm;
}

static grpc_compression_algorithm compression_algorithm_for_level_locked(
    grpc_call *call, grpc_compression_level level) {
  return grpc_compression_algorithm_for_level(level,
                                              call->encodings_accepted_by_peer);
}

uint32_t grpc_call_test_only_get_message_flags(grpc_call *call) {
  uint32_t flags;
  gpr_mu_lock(&call->mu);
  flags = call->test_only_last_message_flags;
  gpr_mu_unlock(&call->mu);
  return flags;
}

static void destroy_encodings_accepted_by_peer(void *p) { return; }

static void set_encodings_accepted_by_peer(grpc_exec_ctx *exec_ctx,
                                           grpc_call *call, grpc_mdelem mdel) {
  size_t i;
  grpc_compression_algorithm algorithm;
  grpc_slice_buffer accept_encoding_parts;
  grpc_slice accept_encoding_slice;
  void *accepted_user_data;

  accepted_user_data =
      grpc_mdelem_get_user_data(mdel, destroy_encodings_accepted_by_peer);
  if (accepted_user_data != NULL) {
    call->encodings_accepted_by_peer =
        (uint32_t)(((uintptr_t)accepted_user_data) - 1);
    return;
  }

  accept_encoding_slice = GRPC_MDVALUE(mdel);
  grpc_slice_buffer_init(&accept_encoding_parts);
  grpc_slice_split(accept_encoding_slice, ",", &accept_encoding_parts);

  /* No need to zero call->encodings_accepted_by_peer: grpc_call_create already
   * zeroes the whole grpc_call */
  /* Always support no compression */
  GPR_BITSET(&call->encodings_accepted_by_peer, GRPC_COMPRESS_NONE);
  for (i = 0; i < accept_encoding_parts.count; i++) {
    grpc_slice accept_encoding_entry_slice = accept_encoding_parts.slices[i];
    if (grpc_compression_algorithm_parse(accept_encoding_entry_slice,
                                         &algorithm)) {
      GPR_BITSET(&call->encodings_accepted_by_peer, algorithm);
    } else {
      char *accept_encoding_entry_str =
          grpc_slice_to_c_string(accept_encoding_entry_slice);
      gpr_log(GPR_ERROR,
              "Invalid entry in accept encoding metadata: '%s'. Ignoring.",
              accept_encoding_entry_str);
      gpr_free(accept_encoding_entry_str);
    }
  }

  grpc_slice_buffer_destroy_internal(exec_ctx, &accept_encoding_parts);

  grpc_mdelem_set_user_data(
      mdel, destroy_encodings_accepted_by_peer,
      (void *)(((uintptr_t)call->encodings_accepted_by_peer) + 1));
}

uint32_t grpc_call_test_only_get_encodings_accepted_by_peer(grpc_call *call) {
  uint32_t encodings_accepted_by_peer;
  gpr_mu_lock(&call->mu);
  encodings_accepted_by_peer = call->encodings_accepted_by_peer;
  gpr_mu_unlock(&call->mu);
  return encodings_accepted_by_peer;
}

grpc_compression_algorithm grpc_call_compression_for_level(
    grpc_call *call, grpc_compression_level level) {
  gpr_mu_lock(&call->mu);
  grpc_compression_algorithm algo =
      compression_algorithm_for_level_locked(call, level);
  gpr_mu_unlock(&call->mu);
  return algo;
}

/*******************************************************************************
 * METADATA HANDLING
 */

static grpc_linked_mdelem *linked_from_md(grpc_metadata *md) {
  return (grpc_linked_mdelem *)&md->internal_data;
}

static grpc_metadata *get_md_elem(grpc_metadata *metadata,
                                  grpc_metadata *additional_metadata, int i,
                                  int count) {
  grpc_metadata *res =
      i < count ? &metadata[i] : &additional_metadata[i - count];
  GPR_ASSERT(res);
  return res;
}

static int prepare_application_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call *call, int count,
    grpc_metadata *metadata, int is_trailing, int prepend_extra_metadata,
    grpc_metadata *additional_metadata, int additional_metadata_count) {
  int total_count = count + additional_metadata_count;
  int i;
  grpc_metadata_batch *batch =
      &call->metadata_batch[0 /* is_receiving */][is_trailing];
  for (i = 0; i < total_count; i++) {
    const grpc_metadata *md =
        get_md_elem(metadata, additional_metadata, i, count);
    grpc_linked_mdelem *l = (grpc_linked_mdelem *)&md->internal_data;
    GPR_ASSERT(sizeof(grpc_linked_mdelem) == sizeof(md->internal_data));
    if (!GRPC_LOG_IF_ERROR("validate_metadata",
                           grpc_validate_header_key_is_legal(md->key))) {
      break;
    } else if (!grpc_is_binary_header(md->key) &&
               !GRPC_LOG_IF_ERROR(
                   "validate_metadata",
                   grpc_validate_header_nonbin_value_is_legal(md->value))) {
      break;
    }
    l->md = grpc_mdelem_from_grpc_metadata(exec_ctx, (grpc_metadata *)md);
  }
  if (i != total_count) {
    for (int j = 0; j < i; j++) {
      const grpc_metadata *md =
          get_md_elem(metadata, additional_metadata, j, count);
      grpc_linked_mdelem *l = (grpc_linked_mdelem *)&md->internal_data;
      GRPC_MDELEM_UNREF(exec_ctx, l->md);
    }
    return 0;
  }
  if (prepend_extra_metadata) {
    if (call->send_extra_metadata_count == 0) {
      prepend_extra_metadata = 0;
    } else {
      for (i = 0; i < call->send_extra_metadata_count; i++) {
        GRPC_LOG_IF_ERROR("prepare_application_metadata",
                          grpc_metadata_batch_link_tail(
                              exec_ctx, batch, &call->send_extra_metadata[i]));
      }
    }
  }
  for (i = 0; i < total_count; i++) {
    grpc_metadata *md = get_md_elem(metadata, additional_metadata, i, count);
    GRPC_LOG_IF_ERROR(
        "prepare_application_metadata",
        grpc_metadata_batch_link_tail(exec_ctx, batch, linked_from_md(md)));
  }
  call->send_extra_metadata_count = 0;

  return 1;
}

/* we offset status by a small amount when storing it into transport metadata
   as metadata cannot store a 0 value (which is used as OK for grpc_status_codes
   */
#define STATUS_OFFSET 1
static void destroy_status(void *ignored) {}

static uint32_t decode_status(grpc_mdelem md) {
  uint32_t status;
  void *user_data;
  if (grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_0)) return 0;
  if (grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_1)) return 1;
  if (grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_2)) return 2;
  user_data = grpc_mdelem_get_user_data(md, destroy_status);
  if (user_data != NULL) {
    status = ((uint32_t)(intptr_t)user_data) - STATUS_OFFSET;
  } else {
    if (!grpc_parse_slice_to_uint32(GRPC_MDVALUE(md), &status)) {
      status = GRPC_STATUS_UNKNOWN; /* could not parse status code */
    }
    grpc_mdelem_set_user_data(md, destroy_status,
                              (void *)(intptr_t)(status + STATUS_OFFSET));
  }
  return status;
}

static grpc_compression_algorithm decode_compression(grpc_mdelem md) {
  grpc_compression_algorithm algorithm =
      grpc_compression_algorithm_from_slice(GRPC_MDVALUE(md));
  if (algorithm == GRPC_COMPRESS_ALGORITHMS_COUNT) {
    char *md_c_str = grpc_slice_to_c_string(GRPC_MDVALUE(md));
    gpr_log(GPR_ERROR,
            "Invalid incoming compression algorithm: '%s'. Interpreting "
            "incoming data as uncompressed.",
            md_c_str);
    gpr_free(md_c_str);
    return GRPC_COMPRESS_NONE;
  }
  return algorithm;
}

static void recv_common_filter(grpc_exec_ctx *exec_ctx, grpc_call *call,
                               grpc_metadata_batch *b) {
  if (b->idx.named.grpc_status != NULL) {
    GPR_TIMER_BEGIN("status", 0);
    uint32_t status_code = decode_status(b->idx.named.grpc_status->md);
    grpc_error *error =
        status_code == GRPC_STATUS_OK
            ? GRPC_ERROR_NONE
            : grpc_error_set_int(GRPC_ERROR_CREATE("Error received from peer"),
                                 GRPC_ERROR_INT_GRPC_STATUS,
                                 (intptr_t)status_code);

    if (b->idx.named.grpc_message != NULL) {
      char *msg =
          grpc_slice_to_c_string(GRPC_MDVALUE(b->idx.named.grpc_message->md));
      error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, msg);
      gpr_free(msg);
      grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.grpc_message);
    } else {
      error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, "");
    }

    set_status_from_error(exec_ctx, call, STATUS_FROM_WIRE, error);
    grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.grpc_status);
    GPR_TIMER_END("status", 0);
  }
}

static void publish_app_metadata(grpc_call *call, grpc_metadata_batch *b,
                                 int is_trailing) {
  if (b->list.count == 0) return;
  GPR_TIMER_BEGIN("publish_app_metadata", 0);
  grpc_metadata_array *dest;
  grpc_metadata *mdusr;
  dest = call->buffered_metadata[is_trailing];
  if (dest->count + b->list.count > dest->capacity) {
    dest->capacity =
        GPR_MAX(dest->capacity + b->list.count, dest->capacity * 3 / 2);
    dest->metadata =
        gpr_realloc(dest->metadata, sizeof(grpc_metadata) * dest->capacity);
  }
  for (grpc_linked_mdelem *l = b->list.head; l != NULL; l = l->next) {
    mdusr = &dest->metadata[dest->count++];
    /* we pass back borrowed slices that are valid whilst the call is valid */
    mdusr->key = GRPC_MDKEY(l->md);
    mdusr->value = GRPC_MDVALUE(l->md);
  }
  GPR_TIMER_END("publish_app_metadata", 0);
}

static void recv_initial_filter(grpc_exec_ctx *exec_ctx, grpc_call *call,
                                grpc_metadata_batch *b) {
  recv_common_filter(exec_ctx, call, b);

  if (b->idx.named.grpc_encoding != NULL) {
    GPR_TIMER_BEGIN("incoming_compression_algorithm", 0);
    set_incoming_compression_algorithm(
        call, decode_compression(b->idx.named.grpc_encoding->md));
    GPR_TIMER_END("incoming_compression_algorithm", 0);
    grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.grpc_encoding);
  }

  if (b->idx.named.grpc_accept_encoding != NULL) {
    GPR_TIMER_BEGIN("encodings_accepted_by_peer", 0);
    set_encodings_accepted_by_peer(exec_ctx, call,
                                   b->idx.named.grpc_accept_encoding->md);
    grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.grpc_accept_encoding);
    GPR_TIMER_END("encodings_accepted_by_peer", 0);
  }

  publish_app_metadata(call, b, false);
}

static void recv_trailing_filter(grpc_exec_ctx *exec_ctx, void *args,
                                 grpc_metadata_batch *b) {
  grpc_call *call = args;
  recv_common_filter(exec_ctx, call, b);
  publish_app_metadata(call, b, true);
}

grpc_call_stack *grpc_call_get_call_stack(grpc_call *call) {
  return CALL_STACK_FROM_CALL(call);
}

/*******************************************************************************
 * STREAM RECEIVE PATH: COMMON
 */

static void receiving_stream_ready(grpc_exec_ctx *exec_ctx, void *bctlp,
                                   grpc_error *error) {
  batch_control *bctl = bctlp;
  grpc_call *call = bctl->call;

  gpr_mu_lock(&call->mu);
  if (error != GRPC_ERROR_NONE) {
    cancel_with_error(exec_ctx, call, GRPC_ERROR_REF(error));
  }
  if (call->has_initial_md_been_received || error != GRPC_ERROR_NONE ||
      call->receiving_stream == NULL) {
    gpr_mu_unlock(&call->mu);
    switch (call->recv_mode) {
      case SENDRECV_IDLE:
        GPR_UNREACHABLE_CODE(return );
      case SENDRECV_FULL_MESSAGE:
        process_data_after_md(exec_ctx, bctlp);
        break;
      case SENDRECV_INCREMENTAL:
        process_incremental_data_after_md(exec_ctx, bctlp);
        break;
    }
  } else {
    call->saved_receiving_stream_ready_bctlp = bctlp;
    gpr_mu_unlock(&call->mu);
  }
}

/*******************************************************************************
 * STREAM RECEIVE PATH: FULL MESSAGES
 */

static void continue_receiving_slices(grpc_exec_ctx *exec_ctx,
                                      batch_control *bctl) {
  grpc_call *call = bctl->call;
  for (;;) {
    size_t remaining =
        call->receiving_stream->length -
        (*call->receiving.full.buffer)->data.raw.slice_buffer.length;
    if (remaining == 0) {
      call->recv_mode = SENDRECV_IDLE;
      grpc_byte_stream_destroy(exec_ctx, call->receiving_stream);
      call->receiving_stream = NULL;
      finish_batch_step(exec_ctx, bctl);
      return;
    }
    grpc_closure_init(&call->receiving_next_step, receiving_slice_ready, bctl,
                      grpc_schedule_on_exec_ctx);
    if (grpc_byte_stream_next_slice(exec_ctx, call->receiving_stream,
                                    &call->receiving.full.slice, remaining,
                                    &call->receiving_next_step)) {
      grpc_slice_buffer_add(
          &(*call->receiving.full.buffer)->data.raw.slice_buffer,
          call->receiving.full.slice);
    } else {
      return;
    }
  }
}

static void receiving_slice_ready(grpc_exec_ctx *exec_ctx, void *bctlp,
                                  grpc_error *error) {
  batch_control *bctl = bctlp;
  grpc_call *call = bctl->call;

  if (error == GRPC_ERROR_NONE) {
    grpc_slice_buffer_add(
        &(*call->receiving.full.buffer)->data.raw.slice_buffer,
        call->receiving.full.slice);
    continue_receiving_slices(exec_ctx, bctl);
  } else {
    if (grpc_trace_operation_failures) {
      GRPC_LOG_IF_ERROR("receiving_slice_ready", GRPC_ERROR_REF(error));
    }
    grpc_byte_stream_destroy(exec_ctx, call->receiving_stream);
    call->receiving_stream = NULL;
    grpc_byte_buffer_destroy(*call->receiving.full.buffer);
    *call->receiving.full.buffer = NULL;
    finish_batch_step(exec_ctx, bctl);
  }
}

static void process_data_after_md(grpc_exec_ctx *exec_ctx,
                                  batch_control *bctl) {
  grpc_call *call = bctl->call;
  if (call->receiving_stream == NULL) {
    *call->receiving.full.buffer = NULL;
    call->recv_mode = SENDRECV_IDLE;
    finish_batch_step(exec_ctx, bctl);
  } else {
    call->test_only_last_message_flags = call->receiving_stream->flags;
    if ((call->receiving_stream->flags & GRPC_WRITE_INTERNAL_COMPRESS) &&
        (call->incoming_compression_algorithm > GRPC_COMPRESS_NONE)) {
      *call->receiving.full.buffer = grpc_raw_compressed_byte_buffer_create(
          NULL, 0, call->incoming_compression_algorithm);
    } else {
      *call->receiving.full.buffer = grpc_raw_byte_buffer_create(NULL, 0);
    }
    continue_receiving_slices(exec_ctx, bctl);
    /* early out */
    return;
  }
}

/*******************************************************************************
 * STREAM RECEIVE PATH: INCREMENTAL MESSAGES
 */

static void maybe_continue_incremental_recv(grpc_exec_ctx *exec_ctx,
                                            grpc_call *call);

static void got_next_incr_slice(grpc_exec_ctx *exec_ctx, void *callp,
                                grpc_error *error) {
  grpc_call *call = callp;
  gpr_mu_lock(&call->mu);
  GPR_ASSERT(call->receiving.incremental.buffer_progress.raw.next_slice_state ==
             NEXT_SLICE_REQUESTED);
  call->receiving.incremental.buffer_progress.raw.next_slice_state =
      NEXT_SLICE_PRESENT;
  maybe_continue_incremental_recv(exec_ctx, call);
  gpr_mu_unlock(&call->mu);
}

static void pull_reaped(grpc_exec_ctx *exec_ctx, void *callp,
                        grpc_cq_completion *completion) {}

static void continue_incremental_recv_raw(grpc_exec_ctx *exec_ctx,
                                          grpc_call *call) {
  switch (call->receiving.incremental.buffer_progress.raw.next_slice_state) {
    case NEXT_SLICE_REQUESTED:
      break;
    case NEXT_SLICE_PRESENT:
      grpc_slice_buffer_add(
          &call->receiving.incremental.pull_target->data.raw.slice_buffer,
          call->receiving.incremental.next_slice);
      call->receiving.incremental.remaining -=
          (uint32_t)GRPC_SLICE_LENGTH(call->receiving.incremental.next_slice);
      call->receiving.incremental.buffer_progress.raw.next_slice_state =
          NEXT_SLICE_EMPTY;
    /* fall through */
    case NEXT_SLICE_EMPTY:
      grpc_closure_init(&call->receiving_next_step, got_next_incr_slice, call,
                        grpc_schedule_on_exec_ctx);
      while (call->receiving.incremental.remaining != 0) {
        if (grpc_byte_stream_next_slice(exec_ctx, call->receiving_stream,
                                        &call->receiving.incremental.next_slice,
                                        call->receiving.incremental.remaining,
                                        &call->receiving_next_step)) {
          grpc_slice_buffer_add(
              &call->receiving.incremental.pull_target->data.raw.slice_buffer,
              call->receiving.incremental.next_slice);
          call->receiving.incremental.remaining -= (uint32_t)GRPC_SLICE_LENGTH(
              call->receiving.incremental.next_slice);
        } else {
          call->receiving.incremental.buffer_progress.raw.next_slice_state =
              NEXT_SLICE_REQUESTED;
          break;
        }
      }
      if (call->receiving.incremental.pull_target->data.raw.slice_buffer
              .length > 0) {
        call->receiving.incremental.pull_target = NULL;
        grpc_cq_end_op(exec_ctx, call->cq, call->receiving.incremental.pull_tag,
                       GRPC_ERROR_NONE, pull_reaped, call,
                       &call->receiving.incremental.cq_completion);
        if (call->receiving.incremental.remaining == 0) {
          call->recv_mode = SENDRECV_IDLE;
          grpc_byte_stream_destroy(exec_ctx, call->receiving_stream);
          call->receiving_stream = NULL;
        }
      }
      break;
  }
}

static void noop_stream_ref(void *arg) {}
static void noop_stream_unref(grpc_exec_ctx *exec_ctx, void *arg) {}
static void continue_incremental_recv_iovec(grpc_exec_ctx *exec_ctx,
                                            grpc_call *call);

static const grpc_slice_refcount_vtable recv_vtable = {
    noop_stream_ref, noop_stream_unref, grpc_slice_default_eq_impl,
    grpc_slice_default_hash_impl};
static grpc_slice_refcount recv_refcount = {&recv_vtable, &recv_refcount};

static void got_next_iovec_slice(grpc_exec_ctx *exec_ctx, void *arg,
                                 grpc_error *error) {
  grpc_call *call = arg;
  gpr_mu_lock(&call->mu);
  continue_incremental_recv_iovec(exec_ctx, call);
  gpr_mu_unlock(&call->mu);
}

static void continue_incremental_recv_iovec(grpc_exec_ctx *exec_ctx,
                                            grpc_call *call) {
  grpc_slice *slice = &call->receiving.incremental.next_slice;

  grpc_closure_init(&call->receiving_next_step, got_next_iovec_slice, call,
                    grpc_schedule_on_exec_ctx);
  do {
    if (call->receiving.incremental.buffer_progress.iovec.pull_idx ==
        call->receiving.incremental.pull_target->data.iovec.elem_count) {
      call->receiving.incremental.pull_target = NULL;
      grpc_cq_end_op(exec_ctx, call->cq, call->receiving.incremental.pull_tag,
                     GRPC_ERROR_NONE, pull_reaped, call,
                     &call->receiving.incremental.cq_completion);
      if (call->receiving.incremental.remaining == 0) {
        grpc_byte_stream_destroy(exec_ctx, call->receiving_stream);
        call->receiving_stream = NULL;
      }
      break;
    }

    slice->refcount = &recv_refcount;
    grpc_bb_iovec_elem elem =
        call->receiving.incremental.pull_target->data.iovec.elems
            [call->receiving.incremental.buffer_progress.iovec.pull_idx++];
    slice->data.refcounted.bytes = elem.base;
    slice->data.refcounted.length = elem.len;
    call->receiving.incremental.remaining -= (uint32_t)elem.len;
  } while (grpc_byte_stream_next_slice(exec_ctx, call->receiving_stream, slice,
                                       GRPC_BYTE_STREAM_DIRECT_DATA_PLACEMENT,
                                       &call->receiving_next_step));
}

static void maybe_continue_incremental_recv(grpc_exec_ctx *exec_ctx,
                                            grpc_call *call) {
  if (call->receiving.incremental.length_target == NULL &&
      call->receiving.incremental.pull_target != NULL) {
    switch (call->receiving.incremental.pull_target->type) {
      case GRPC_BB_RAW:
        continue_incremental_recv_raw(exec_ctx, call);
        break;
      case GRPC_BB_IOVEC:
        continue_incremental_recv_iovec(exec_ctx, call);
        break;
    }
  }
}

grpc_call_error grpc_call_incremental_message_reader_pull(
    grpc_call *call, grpc_byte_buffer *buffer, void *tag) {
  GRPC_API_TRACE(
      "grpc_call_incremental_message_reader_pull(call=%p, buffer=%p, tag=%p)",
      3, (call, buffer, tag));

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_mu_lock(&call->mu);
  if (call->recv_mode != SENDRECV_INCREMENTAL) {
    gpr_mu_unlock(&call->mu);
    grpc_exec_ctx_finish(&exec_ctx);
    return GRPC_CALL_ERROR_NOT_CURRENTLY_INCREMENTAL;
  }
  if (call->receiving.incremental.pull_target != NULL) {
    gpr_mu_unlock(&call->mu);
    grpc_exec_ctx_finish(&exec_ctx);
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }
  grpc_cq_begin_op(call->cq, tag);
  call->receiving.incremental.pull_target = buffer;
  call->receiving.incremental.pull_tag = tag;
  switch (call->receiving.incremental.pull_target->type) {
    case GRPC_BB_RAW:
      break;
    case GRPC_BB_IOVEC:
      call->receiving.incremental.buffer_progress.iovec.pull_idx = 0;
      break;
  }
  maybe_continue_incremental_recv(&exec_ctx, call);
  gpr_mu_unlock(&call->mu);
  grpc_exec_ctx_finish(&exec_ctx);
  return GRPC_CALL_OK;
}

static void process_incremental_data_after_md(grpc_exec_ctx *exec_ctx,
                                              batch_control *bctl) {
  grpc_call *call = bctl->call;
  if (call->receiving_stream == NULL) {
    call->recv_mode = SENDRECV_IDLE;
    add_batch_error(exec_ctx, bctl, GRPC_ERROR_END_OF_STREAM);
  } else {
    call->test_only_last_message_flags = call->receiving_stream->flags;
    *call->receiving.incremental.length_target =
        call->receiving.incremental.remaining = call->receiving_stream->length;
    call->receiving.incremental.length_target = NULL;
    maybe_continue_incremental_recv(exec_ctx, call);
  }
  finish_batch_step(exec_ctx, bctl);
}

/*******************************************************************************
 * STREAM SEND PATH: INCREMENTAL MESSAGES
 */

static grpc_call *incwr_call_from_stream(grpc_byte_stream *byte_stream) {
  intptr_t offset =
      (intptr_t) & ((grpc_call *)NULL)->sending.incremental.stream;
  return (grpc_call *)(((char *)byte_stream) - offset);
}

static grpc_call *incwr_call_from_iovec_slice_refcount(
    grpc_slice_refcount *rc) {
  intptr_t offset =
      (intptr_t) &
      ((grpc_call *)NULL)
          ->sending.incremental.buffer_progress.iovec.slice_refcount;
  return (grpc_call *)(((char *)rc) - offset);
}

static bool incwr_at_end_of_slice_buffer(grpc_call *call) {
  return call->sending.incremental.buffer_progress.raw.slice_index ==
         call->sending.incremental.buffer->data.raw.slice_buffer.count;
}

static bool incwr_at_end_of_iovec(grpc_call *call) {
  return call->sending.incremental.buffer_progress.iovec.elem_index ==
         call->sending.incremental.buffer->data.iovec.elem_count;
}

static void incwr_published_send(grpc_exec_ctx *exec_ctx, void *callp,
                                 grpc_cq_completion *unused) {}

static void iovec_slice_ref(void *p) {
  grpc_call *call = incwr_call_from_iovec_slice_refcount(p);
  gpr_ref(&call->sending.incremental.buffer_progress.iovec.waiting_refs);
}

static void iovec_slice_unref(grpc_exec_ctx *exec_ctx, void *p) {
  grpc_call *call = incwr_call_from_iovec_slice_refcount(p);
  if (gpr_unref(
          &call->sending.incremental.buffer_progress.iovec.waiting_refs)) {
    GPR_ASSERT(call->sending.incremental.buffer == NULL);
    grpc_cq_end_op(exec_ctx, call->cq, call->sending.incremental.tag,
                   GRPC_ERROR_NONE, incwr_published_send, call,
                   &call->sending.incremental.cq_completion);
  }
}

static void incwr_complete_slice(grpc_exec_ctx *exec_ctx, grpc_call *call,
                                 grpc_slice *slice) {
  switch (call->sending.incremental.buffer->type) {
    case GRPC_BB_RAW:
      GPR_ASSERT(!incwr_at_end_of_slice_buffer(call));
      *slice = grpc_slice_ref(
          call->sending.incremental.buffer->data.raw.slice_buffer.slices
              [call->sending.incremental.buffer_progress.raw.slice_index++]);
      if (incwr_at_end_of_slice_buffer(call)) {
        call->sending.incremental.buffer = NULL;
        grpc_cq_end_op(exec_ctx, call->cq, call->sending.incremental.tag,
                       GRPC_ERROR_NONE, incwr_published_send, call,
                       &call->sending.incremental.cq_completion);
      }
      break;
    case GRPC_BB_IOVEC:
      GPR_ASSERT(!incwr_at_end_of_iovec(call));
      slice->refcount =
          &call->sending.incremental.buffer_progress.iovec.slice_refcount;
      iovec_slice_ref(
          &call->sending.incremental.buffer_progress.iovec.slice_refcount);
      grpc_bb_iovec_elem elem =
          call->sending.incremental.buffer->data.iovec.elems
              [call->sending.incremental.buffer_progress.iovec.elem_index++];
      slice->data.refcounted.bytes = elem.base;
      slice->data.refcounted.length = elem.len;
      if (incwr_at_end_of_iovec(call)) {
        call->sending.incremental.buffer = NULL;
      }
      break;
  }
}

static bool incwr_bs_next_slice(grpc_exec_ctx *exec_ctx,
                                grpc_byte_stream *byte_stream,
                                grpc_slice *slice, size_t max_size_hint,
                                grpc_closure *on_complete) {
  GPR_ASSERT(max_size_hint != GRPC_BYTE_STREAM_DIRECT_DATA_PLACEMENT &&
             "Direct placement transport fetches are not supported");
  grpc_call *call = incwr_call_from_stream(byte_stream);
  gpr_mu_lock(&call->mu);
  GPR_ASSERT(call->sending.incremental.on_next == NULL);
  if (call->sending.incremental.buffer != NULL) {
    incwr_complete_slice(exec_ctx, call, slice);
    gpr_mu_unlock(&call->mu);
    return true;
  }
  call->sending.incremental.next_is_buffer = false;
  call->sending.incremental.next_slice = slice;
  call->sending.incremental.on_next = on_complete;
  gpr_mu_unlock(&call->mu);
  return false;
}

static void incwr_bs_destroy(grpc_exec_ctx *exec_ctx,
                             grpc_byte_stream *byte_stream) {
  abort();
}

static const grpc_slice_refcount_vtable iovec_slice_vtable = {
    iovec_slice_ref, iovec_slice_unref, grpc_slice_default_eq_impl,
    grpc_slice_default_hash_impl};

grpc_call_error grpc_call_incremental_message_writer_push(
    grpc_call *call, grpc_byte_buffer *buffer, void *tag) {
  GRPC_API_TRACE(
      "grpc_call_incremental_message_writer_push(call=%p, buffer=%p[len=%d], "
      "tag=%p)",
      4,
      (call, buffer, buffer ? (int)grpc_byte_buffer_length(buffer) : -1, tag));

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_mu_lock(&call->mu);
  if (call->send_mode != SENDRECV_INCREMENTAL) {
    gpr_mu_unlock(&call->mu);
    grpc_exec_ctx_finish(&exec_ctx);
    return GRPC_CALL_ERROR_NOT_CURRENTLY_INCREMENTAL;
  }
  if (call->sending.incremental.buffer != NULL) {
    gpr_mu_unlock(&call->mu);
    grpc_exec_ctx_finish(&exec_ctx);
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }
  size_t bb_len = grpc_byte_buffer_length(buffer);
  if (bb_len > call->sending.incremental.awaiting_bytes) {
    gpr_mu_unlock(&call->mu);
    grpc_exec_ctx_finish(&exec_ctx);
    return GRPC_CALL_ERROR_INVALID_MESSAGE;
  }
  call->sending.incremental.awaiting_bytes -= (uint32_t)bb_len;
  bool send_done = call->sending.incremental.awaiting_bytes == 0;
  grpc_cq_begin_op(call->cq, tag);
  call->sending.incremental.buffer = buffer;
  call->sending.incremental.tag = tag;
  switch (buffer->type) {
    case GRPC_BB_RAW:
      call->sending.incremental.buffer_progress.raw.slice_index = 0;
      break;
    case GRPC_BB_IOVEC:
      call->sending.incremental.buffer_progress.iovec.elem_index = 0;
      call->sending.incremental.buffer_progress.iovec.slice_refcount.vtable =
          &iovec_slice_vtable;
      call->sending.incremental.buffer_progress.iovec.slice_refcount
          .sub_refcount =
          &call->sending.incremental.buffer_progress.iovec.slice_refcount;
      break;
  }
  if (call->sending.incremental.on_next != NULL) {
    incwr_complete_slice(&exec_ctx, call, call->sending.incremental.next_slice);
    grpc_closure_sched(&exec_ctx, call->sending.incremental.on_next,
                       GRPC_ERROR_NONE);
    call->sending.incremental.on_next = NULL;
  }
  gpr_mu_unlock(&call->mu);
  if (send_done) {
    finish_batch_step(&exec_ctx, call->sending.incremental.bctl);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  return GRPC_CALL_OK;
}

/*******************************************************************************
 * BATCH API IMPLEMENTATION
 */

static void set_status_value_directly(grpc_status_code status, void *dest) {
  *(grpc_status_code *)dest = status;
}

static void set_cancelled_value(grpc_status_code status, void *dest) {
  *(int *)dest = (status != GRPC_STATUS_OK);
}

static bool are_write_flags_valid(uint32_t flags) {
  /* check that only bits in GRPC_WRITE_(INTERNAL?)_USED_MASK are set */
  const uint32_t allowed_write_positions =
      (GRPC_WRITE_USED_MASK | GRPC_WRITE_INTERNAL_USED_MASK);
  const uint32_t invalid_positions = ~allowed_write_positions;
  return !(flags & invalid_positions);
}

static bool are_initial_metadata_flags_valid(uint32_t flags, bool is_client) {
  /* check that only bits in GRPC_WRITE_(INTERNAL?)_USED_MASK are set */
  uint32_t invalid_positions = ~GRPC_INITIAL_METADATA_USED_MASK;
  if (!is_client) {
    invalid_positions |= GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST;
  }
  return !(flags & invalid_positions);
}

static batch_control *allocate_batch_control(grpc_call *call) {
  size_t i;
  for (i = 0; i < MAX_CONCURRENT_BATCHES; i++) {
    if ((call->used_batches & (1 << i)) == 0) {
      call->used_batches = (uint8_t)(call->used_batches | (uint8_t)(1 << i));
      return &call->active_batches[i];
    }
  }
  return NULL;
}

static void finish_batch_completion(grpc_exec_ctx *exec_ctx, void *user_data,
                                    grpc_cq_completion *storage) {
  batch_control *bctl = user_data;
  grpc_call *call = bctl->call;
  gpr_mu_lock(&call->mu);
  call->used_batches = (uint8_t)(
      call->used_batches & ~(uint8_t)(1 << (bctl - call->active_batches)));
  gpr_mu_unlock(&call->mu);
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "completion");
}

static grpc_error *consolidate_batch_errors(batch_control *bctl) {
  size_t n = (size_t)gpr_atm_no_barrier_load(&bctl->num_errors);
  if (n == 0) {
    return GRPC_ERROR_NONE;
  } else if (n == 1) {
    /* Skip creating a composite error in the case that only one error was
       logged */
    grpc_error *e = bctl->errors[0];
    bctl->errors[0] = NULL;
    return e;
  } else {
    grpc_error *error =
        GRPC_ERROR_CREATE_REFERENCING("Call batch failed", bctl->errors, n);
    for (size_t i = 0; i < n; i++) {
      GRPC_ERROR_UNREF(bctl->errors[i]);
      bctl->errors[i] = NULL;
    }
    return error;
  }
}

static void post_batch_completion(grpc_exec_ctx *exec_ctx,
                                  batch_control *bctl) {
  grpc_call *child_call;
  grpc_call *next_child_call;
  grpc_call *call = bctl->call;
  grpc_error *error = consolidate_batch_errors(bctl);

  gpr_mu_lock(&call->mu);

  if (error != GRPC_ERROR_NONE) {
    set_status_from_error(exec_ctx, call, STATUS_FROM_CORE,
                          GRPC_ERROR_REF(error));
  }

  if (bctl->send_initial_metadata) {
    grpc_metadata_batch_destroy(
        exec_ctx,
        &call->metadata_batch[0 /* is_receiving */][0 /* is_trailing */]);
  }
  if (bctl->send_message) {
    call->send_mode = SENDRECV_IDLE;
  }
  if (bctl->send_final_op) {
    grpc_metadata_batch_destroy(
        exec_ctx,
        &call->metadata_batch[0 /* is_receiving */][1 /* is_trailing */]);
  }
  if (bctl->recv_final_op) {
    grpc_metadata_batch *md =
        &call->metadata_batch[1 /* is_receiving */][1 /* is_trailing */];
    recv_trailing_filter(exec_ctx, call, md);

    call->received_final_op = true;
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

    if (call->is_client) {
      get_final_status(call, set_status_value_directly,
                       call->final_op.client.status,
                       call->final_op.client.status_details);
    } else {
      get_final_status(call, set_cancelled_value,
                       call->final_op.server.cancelled, NULL);
    }

    GRPC_ERROR_UNREF(error);
    error = GRPC_ERROR_NONE;
  }
  gpr_mu_unlock(&call->mu);

  if (bctl->is_notify_tag_closure) {
    /* unrefs bctl->error */
    grpc_closure_run(exec_ctx, bctl->notify_tag, error);
    gpr_mu_lock(&call->mu);
    bctl->call->used_batches =
        (uint8_t)(bctl->call->used_batches &
                  ~(uint8_t)(1 << (bctl - bctl->call->active_batches)));
    gpr_mu_unlock(&call->mu);
    GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "completion");
  } else {
    /* unrefs bctl->error */
    grpc_cq_end_op(exec_ctx, bctl->call->cq, bctl->notify_tag, error,
                   finish_batch_completion, bctl, &bctl->cq_completion);
  }
}

static void finish_batch_step(grpc_exec_ctx *exec_ctx, batch_control *bctl) {
  if (gpr_unref(&bctl->steps_to_complete)) {
    post_batch_completion(exec_ctx, bctl);
  }
}

static void validate_filtered_metadata(grpc_exec_ctx *exec_ctx,
                                       batch_control *bctl) {
  grpc_call *call = bctl->call;
  /* validate call->incoming_compression_algorithm */
  if (call->incoming_compression_algorithm != GRPC_COMPRESS_NONE) {
    const grpc_compression_algorithm algo =
        call->incoming_compression_algorithm;
    char *error_msg = NULL;
    const grpc_compression_options compression_options =
        grpc_channel_compression_options(call->channel);
    /* check if algorithm is known */
    if (algo >= GRPC_COMPRESS_ALGORITHMS_COUNT) {
      gpr_asprintf(&error_msg, "Invalid compression algorithm value '%d'.",
                   algo);
      gpr_log(GPR_ERROR, "%s", error_msg);
      cancel_with_status(exec_ctx, call, GRPC_STATUS_UNIMPLEMENTED, error_msg);
    } else if (grpc_compression_options_is_algorithm_enabled(
                   &compression_options, algo) == 0) {
      /* check if algorithm is supported by current channel config */
      char *algo_name = NULL;
      grpc_compression_algorithm_name(algo, &algo_name);
      gpr_asprintf(&error_msg, "Compression algorithm '%s' is disabled.",
                   algo_name);
      gpr_log(GPR_ERROR, "%s", error_msg);
      cancel_with_status(exec_ctx, call, GRPC_STATUS_UNIMPLEMENTED, error_msg);
    } else {
      call->incoming_compression_algorithm = algo;
    }
    gpr_free(error_msg);
  }

  /* make sure the received grpc-encoding is amongst the ones listed in
   * grpc-accept-encoding */
  GPR_ASSERT(call->encodings_accepted_by_peer != 0);
  if (!GPR_BITGET(call->encodings_accepted_by_peer,
                  call->incoming_compression_algorithm)) {
    extern int grpc_compression_trace;
    if (grpc_compression_trace) {
      char *algo_name = NULL;
      grpc_compression_algorithm_name(call->incoming_compression_algorithm,
                                      &algo_name);
      gpr_log(GPR_ERROR,
              "Compression algorithm (grpc-encoding = '%s') not present in "
              "the bitset of accepted encodings (grpc-accept-encodings: "
              "'0x%x')",
              algo_name, call->encodings_accepted_by_peer);
    }
  }
}

static void add_batch_error(grpc_exec_ctx *exec_ctx, batch_control *bctl,
                            grpc_error *error) {
  if (error == GRPC_ERROR_NONE) return;
  int idx = (int)gpr_atm_no_barrier_fetch_add(&bctl->num_errors, 1);
  if (idx == 0) cancel_with_error(exec_ctx, bctl->call, GRPC_ERROR_REF(error));
  bctl->errors[idx] = error;
}

static void receiving_initial_metadata_ready(grpc_exec_ctx *exec_ctx,
                                             void *bctlp, grpc_error *error) {
  batch_control *bctl = bctlp;
  grpc_call *call = bctl->call;

  gpr_mu_lock(&call->mu);

  add_batch_error(exec_ctx, bctl, GRPC_ERROR_REF(error));
  if (error == GRPC_ERROR_NONE) {
    grpc_metadata_batch *md =
        &call->metadata_batch[1 /* is_receiving */][0 /* is_trailing */];
    recv_initial_filter(exec_ctx, call, md);

    /* TODO(ctiller): this could be moved into recv_initial_filter now */
    GPR_TIMER_BEGIN("validate_filtered_metadata", 0);
    validate_filtered_metadata(exec_ctx, bctl);
    GPR_TIMER_END("validate_filtered_metadata", 0);

    if (gpr_time_cmp(md->deadline, gpr_inf_future(md->deadline.clock_type)) !=
            0 &&
        !call->is_client) {
      call->send_deadline =
          gpr_convert_clock_type(md->deadline, GPR_CLOCK_MONOTONIC);
    }
  }

  call->has_initial_md_been_received = true;
  if (call->saved_receiving_stream_ready_bctlp != NULL) {
    call->saved_receiving_stream_ready_bctlp = NULL;
    grpc_closure_sched(
        exec_ctx, grpc_closure_create(receiving_stream_ready,
                                      call->saved_receiving_stream_ready_bctlp,
                                      grpc_schedule_on_exec_ctx),
        error);
  }

  gpr_mu_unlock(&call->mu);

  finish_batch_step(exec_ctx, bctl);
}

static void finish_batch(grpc_exec_ctx *exec_ctx, void *bctlp,
                         grpc_error *error) {
  batch_control *bctl = bctlp;

  add_batch_error(exec_ctx, bctl, GRPC_ERROR_REF(error));
  finish_batch_step(exec_ctx, bctl);
}

static grpc_call_error call_start_batch(grpc_exec_ctx *exec_ctx,
                                        grpc_call *call, const grpc_op *ops,
                                        size_t nops, void *notify_tag,
                                        int is_notify_tag_closure) {
  size_t i;
  const grpc_op *op;
  batch_control *bctl;
  int num_completion_callbacks_needed = 1;
  grpc_call_error error = GRPC_CALL_OK;

  // sent_initial_metadata guards against variable reuse.
  grpc_metadata compression_md;

  GPR_TIMER_BEGIN("grpc_call_start_batch", 0);

  GRPC_CALL_LOG_BATCH(GPR_INFO, call, ops, nops, notify_tag);

  /* TODO(ctiller): this feels like it could be made lock-free */
  gpr_mu_lock(&call->mu);
  bctl = allocate_batch_control(call);
  memset(bctl, 0, sizeof(*bctl));
  bctl->call = call;
  bctl->notify_tag = notify_tag;
  bctl->is_notify_tag_closure = (uint8_t)(is_notify_tag_closure != 0);

  grpc_transport_stream_op *stream_op = &bctl->op;
  memset(stream_op, 0, sizeof(*stream_op));
  stream_op->covered_by_poller = true;

  if (nops == 0) {
    GRPC_CALL_INTERNAL_REF(call, "completion");
    if (!is_notify_tag_closure) {
      grpc_cq_begin_op(call->cq, notify_tag);
    }
    gpr_mu_unlock(&call->mu);
    post_batch_completion(exec_ctx, bctl);
    error = GRPC_CALL_OK;
    goto done;
  }

  /* rewrite batch ops into a transport op */
  for (i = 0; i < nops; i++) {
    op = &ops[i];
    if (op->reserved != NULL) {
      error = GRPC_CALL_ERROR;
      goto done_with_error;
    }
    switch (op->op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        /* Flag validation: currently allow no flags */
        if (!are_initial_metadata_flags_valid(op->flags, call->is_client)) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->sent_initial_metadata) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        /* process compression level */
        memset(&compression_md, 0, sizeof(compression_md));
        size_t additional_metadata_count = 0;
        grpc_compression_level effective_compression_level;
        bool level_set = false;
        if (op->data.send_initial_metadata.maybe_compression_level.is_set) {
          effective_compression_level =
              op->data.send_initial_metadata.maybe_compression_level.level;
          level_set = true;
        } else {
          const grpc_compression_options copts =
              grpc_channel_compression_options(call->channel);
          level_set = copts.default_level.is_set;
          if (level_set) {
            effective_compression_level = copts.default_level.level;
          }
        }
        if (level_set && !call->is_client) {
          const grpc_compression_algorithm calgo =
              compression_algorithm_for_level_locked(
                  call, effective_compression_level);
          // the following will be picked up by the compress filter and used as
          // the call's compression algorithm.
          compression_md.key = GRPC_MDSTR_GRPC_INTERNAL_ENCODING_REQUEST;
          compression_md.value = grpc_compression_algorithm_slice(calgo);
          additional_metadata_count++;
        }

        if (op->data.send_initial_metadata.count + additional_metadata_count >
            INT_MAX) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        bctl->send_initial_metadata = 1;
        call->sent_initial_metadata = 1;
        if (!prepare_application_metadata(
                exec_ctx, call, (int)op->data.send_initial_metadata.count,
                op->data.send_initial_metadata.metadata, 0, call->is_client,
                &compression_md, (int)additional_metadata_count)) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        /* TODO(ctiller): just make these the same variable? */
        call->metadata_batch[0][0].deadline = call->send_deadline;
        stream_op->send_initial_metadata =
            &call->metadata_batch[0 /* is_receiving */][0 /* is_trailing */];
        stream_op->send_initial_metadata_flags = op->flags;
        break;
      case GRPC_OP_SEND_MESSAGE:
        if (!are_write_flags_valid(op->flags)) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (op->data.send_message.send_message == NULL) {
          error = GRPC_CALL_ERROR_INVALID_MESSAGE;
          goto done_with_error;
        }
        if (call->send_mode != SENDRECV_IDLE) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        bctl->send_message = 1;
        call->send_mode = SENDRECV_FULL_MESSAGE;
        grpc_slice_buffer_stream_init(
            &call->sending.full.stream,
            &op->data.send_message.send_message->data.raw.slice_buffer,
            op->flags);
        stream_op->send_message = &call->sending.full.stream.base;
        break;
      case GRPC_OP_SEND_MESSAGE_INCREMENTAL_START:
        if (!are_write_flags_valid(op->flags)) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->send_mode != SENDRECV_IDLE) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        bctl->send_message = 1;
        call->send_mode = SENDRECV_INCREMENTAL;
        call->sending.incremental.stream.length =
            op->data.send_message_incremental_start.message_length;
        call->sending.incremental.awaiting_bytes =
            op->data.send_message_incremental_start.message_length;
        call->sending.incremental.stream.flags = op->flags;
        call->sending.incremental.stream.next_slice = incwr_bs_next_slice;
        call->sending.incremental.stream.destroy = incwr_bs_destroy;
        call->sending.incremental.bctl = bctl;
        stream_op->send_message = &call->sending.incremental.stream;
        num_completion_callbacks_needed++;
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (!call->is_client) {
          error = GRPC_CALL_ERROR_NOT_ON_SERVER;
          goto done_with_error;
        }
        if (call->sent_final_op) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        bctl->send_final_op = 1;
        call->sent_final_op = 1;
        stream_op->send_trailing_metadata =
            &call->metadata_batch[0 /* is_receiving */][1 /* is_trailing */];
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->is_client) {
          error = GRPC_CALL_ERROR_NOT_ON_CLIENT;
          goto done_with_error;
        }
        if (call->sent_final_op) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        if (op->data.send_status_from_server.trailing_metadata_count >
            INT_MAX) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        bctl->send_final_op = 1;
        call->sent_final_op = 1;
        call->send_extra_metadata_count = 1;
        call->send_extra_metadata[0].md = grpc_channel_get_reffed_status_elem(
            exec_ctx, call->channel, op->data.send_status_from_server.status);
        {
          grpc_error *override_error = GRPC_ERROR_NONE;
          if (op->data.send_status_from_server.status != GRPC_STATUS_OK) {
            override_error = GRPC_ERROR_CREATE("Error from server send status");
          }
          if (op->data.send_status_from_server.status_details != NULL) {
            call->send_extra_metadata[1].md = grpc_mdelem_from_slices(
                exec_ctx, GRPC_MDSTR_GRPC_MESSAGE,
                grpc_slice_ref_internal(
                    *op->data.send_status_from_server.status_details));
            call->send_extra_metadata_count++;
            char *msg = grpc_slice_to_c_string(
                GRPC_MDVALUE(call->send_extra_metadata[1].md));
            override_error = grpc_error_set_str(
                override_error, GRPC_ERROR_STR_GRPC_MESSAGE, msg);
            gpr_free(msg);
          }
          set_status_from_error(exec_ctx, call, STATUS_FROM_API_OVERRIDE,
                                override_error);
        }
        if (!prepare_application_metadata(
                exec_ctx, call,
                (int)op->data.send_status_from_server.trailing_metadata_count,
                op->data.send_status_from_server.trailing_metadata, 1, 1, NULL,
                0)) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        stream_op->send_trailing_metadata =
            &call->metadata_batch[0 /* is_receiving */][1 /* is_trailing */];
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->received_initial_metadata) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        /* IF this is a server, then GRPC_OP_RECV_INITIAL_METADATA *must* come
           from server.c. In that case, it's coming from accept_stream, and in
           that case we're not necessarily covered by a poller. */
        stream_op->covered_by_poller = call->is_client;
        call->received_initial_metadata = 1;
        call->buffered_metadata[0] =
            op->data.recv_initial_metadata.recv_initial_metadata;
        grpc_closure_init(&call->receiving_initial_metadata_ready,
                          receiving_initial_metadata_ready, bctl,
                          grpc_schedule_on_exec_ctx);
        bctl->recv_initial_metadata = 1;
        stream_op->recv_initial_metadata =
            &call->metadata_batch[1 /* is_receiving */][0 /* is_trailing */];
        stream_op->recv_initial_metadata_ready =
            &call->receiving_initial_metadata_ready;
        num_completion_callbacks_needed++;
        break;
      case GRPC_OP_RECV_MESSAGE:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->recv_mode != SENDRECV_IDLE) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        call->recv_mode = SENDRECV_FULL_MESSAGE;
        bctl->recv_message = 1;
        call->receiving.full.buffer = op->data.recv_message.recv_message;
        stream_op->recv_message = &call->receiving_stream;
        grpc_closure_init(&call->receiving_next_step, receiving_stream_ready,
                          bctl, grpc_schedule_on_exec_ctx);
        stream_op->recv_message_ready = &call->receiving_next_step;
        num_completion_callbacks_needed++;
        break;
      case GRPC_OP_RECV_MESSAGE_INCREMENTAL_START:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->recv_mode != SENDRECV_IDLE) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        call->recv_mode = SENDRECV_INCREMENTAL;
        bctl->recv_message = 1;
        call->receiving.incremental.length_target =
            op->data.recv_message_incremental_start.message_length;
        stream_op->recv_message = &call->receiving_stream;
        grpc_closure_init(&call->receiving_next_step, receiving_stream_ready,
                          bctl, grpc_schedule_on_exec_ctx);
        stream_op->recv_message_ready = &call->receiving_next_step;
        num_completion_callbacks_needed++;
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (!call->is_client) {
          error = GRPC_CALL_ERROR_NOT_ON_SERVER;
          goto done_with_error;
        }
        if (call->requested_final_op) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        call->requested_final_op = 1;
        call->buffered_metadata[1] =
            op->data.recv_status_on_client.trailing_metadata;
        call->final_op.client.status = op->data.recv_status_on_client.status;
        call->final_op.client.status_details =
            op->data.recv_status_on_client.status_details;
        bctl->recv_final_op = 1;
        stream_op->recv_trailing_metadata =
            &call->metadata_batch[1 /* is_receiving */][1 /* is_trailing */];
        stream_op->collect_stats =
            &call->final_info.stats.transport_stream_stats;
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->is_client) {
          error = GRPC_CALL_ERROR_NOT_ON_CLIENT;
          goto done_with_error;
        }
        if (call->requested_final_op) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        call->requested_final_op = 1;
        call->final_op.server.cancelled =
            op->data.recv_close_on_server.cancelled;
        bctl->recv_final_op = 1;
        stream_op->recv_trailing_metadata =
            &call->metadata_batch[1 /* is_receiving */][1 /* is_trailing */];
        stream_op->collect_stats =
            &call->final_info.stats.transport_stream_stats;
        break;
    }
  }

  GRPC_CALL_INTERNAL_REF(call, "completion");
  if (!is_notify_tag_closure) {
    grpc_cq_begin_op(call->cq, notify_tag);
  }
  gpr_ref_init(&bctl->steps_to_complete, num_completion_callbacks_needed);

  stream_op->context = call->context;
  grpc_closure_init(&bctl->finish_batch, finish_batch, bctl,
                    grpc_schedule_on_exec_ctx);
  stream_op->on_complete = &bctl->finish_batch;
  gpr_mu_unlock(&call->mu);

  execute_op(exec_ctx, call, stream_op);

done:
  GPR_TIMER_END("grpc_call_start_batch", 0);
  return error;

done_with_error:
  /* reverse any mutations that occured */
  if (bctl->send_initial_metadata) {
    call->sent_initial_metadata = 0;
    grpc_metadata_batch_clear(exec_ctx, &call->metadata_batch[0][0]);
  }
  if (bctl->send_message) {
    call->send_mode = SENDRECV_IDLE;
    grpc_byte_stream_destroy(exec_ctx, &call->sending.full.stream.base);
  }
  if (bctl->send_final_op) {
    call->sent_final_op = 0;
    grpc_metadata_batch_clear(exec_ctx, &call->metadata_batch[0][1]);
  }
  if (bctl->recv_initial_metadata) {
    call->received_initial_metadata = 0;
  }
  if (bctl->recv_message) {
    call->recv_mode = SENDRECV_IDLE;
  }
  if (bctl->recv_final_op) {
    call->requested_final_op = 0;
  }
  gpr_mu_unlock(&call->mu);
  goto done;
}

grpc_call_error grpc_call_start_batch(grpc_call *call, const grpc_op *ops,
                                      size_t nops, void *tag, void *reserved) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call_error err;

  GRPC_API_TRACE(
      "grpc_call_start_batch(call=%p, ops=%p, nops=%lu, tag=%p, "
      "reserved=%p)",
      5, (call, ops, (unsigned long)nops, tag, reserved));

  if (reserved != NULL) {
    err = GRPC_CALL_ERROR;
  } else {
    err = call_start_batch(&exec_ctx, call, ops, nops, tag, 0);
  }

  grpc_exec_ctx_finish(&exec_ctx);
  return err;
}

grpc_call_error grpc_call_start_batch_and_execute(grpc_exec_ctx *exec_ctx,
                                                  grpc_call *call,
                                                  const grpc_op *ops,
                                                  size_t nops,
                                                  grpc_closure *closure) {
  return call_start_batch(exec_ctx, call, ops, nops, closure, 1);
}

/*******************************************************************************
 * CONTEXT APIS
 */

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

/*******************************************************************************
 * UTILITY
 */

const char *grpc_call_error_to_string(grpc_call_error error) {
  switch (error) {
    case GRPC_CALL_ERROR:
      return "GRPC_CALL_ERROR";
    case GRPC_CALL_ERROR_ALREADY_ACCEPTED:
      return "GRPC_CALL_ERROR_ALREADY_ACCEPTED";
    case GRPC_CALL_ERROR_ALREADY_FINISHED:
      return "GRPC_CALL_ERROR_ALREADY_FINISHED";
    case GRPC_CALL_ERROR_ALREADY_INVOKED:
      return "GRPC_CALL_ERROR_ALREADY_INVOKED";
    case GRPC_CALL_ERROR_BATCH_TOO_BIG:
      return "GRPC_CALL_ERROR_BATCH_TOO_BIG";
    case GRPC_CALL_ERROR_INVALID_FLAGS:
      return "GRPC_CALL_ERROR_INVALID_FLAGS";
    case GRPC_CALL_ERROR_INVALID_MESSAGE:
      return "GRPC_CALL_ERROR_INVALID_MESSAGE";
    case GRPC_CALL_ERROR_INVALID_METADATA:
      return "GRPC_CALL_ERROR_INVALID_METADATA";
    case GRPC_CALL_ERROR_NOT_INVOKED:
      return "GRPC_CALL_ERROR_NOT_INVOKED";
    case GRPC_CALL_ERROR_NOT_ON_CLIENT:
      return "GRPC_CALL_ERROR_NOT_ON_CLIENT";
    case GRPC_CALL_ERROR_NOT_ON_SERVER:
      return "GRPC_CALL_ERROR_NOT_ON_SERVER";
    case GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE:
      return "GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE";
    case GRPC_CALL_ERROR_PAYLOAD_TYPE_MISMATCH:
      return "GRPC_CALL_ERROR_PAYLOAD_TYPE_MISMATCH";
    case GRPC_CALL_ERROR_TOO_MANY_OPERATIONS:
      return "GRPC_CALL_ERROR_TOO_MANY_OPERATIONS";
    case GRPC_CALL_ERROR_NOT_CURRENTLY_INCREMENTAL:
      return "GRPC_CALL_ERROR_NOT_CURRENTLY_INCREMENTAL";
    case GRPC_CALL_OK:
      return "GRPC_CALL_OK";
  }
  GPR_UNREACHABLE_CODE(return "GRPC_CALL_ERROR_UNKNOW");
}
