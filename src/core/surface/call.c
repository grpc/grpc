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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/channel/channel_stack.h"
#include "src/core/iomgr/timer.h"
#include "src/core/profiling/timers.h"
#include "src/core/support/string.h"
#include "src/core/surface/api_trace.h"
#include "src/core/surface/call.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/completion_queue.h"

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

typedef struct {
  grpc_ioreq_completion_func on_complete;
  void *user_data;
  int success;
} completed_request;

#define MAX_SEND_EXTRA_METADATA_COUNT 3

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

typedef struct batch_control {
  grpc_call *call;
  grpc_cq_completion cq_completion;
  grpc_closure finish_batch;
  void *notify_tag;
  gpr_refcount steps_to_complete;

  gpr_uint8 send_initial_metadata;
  gpr_uint8 send_message;
  gpr_uint8 send_final_op;
  gpr_uint8 recv_initial_metadata;
  gpr_uint8 recv_message;
  gpr_uint8 recv_final_op;
  gpr_uint8 is_notify_tag_closure;
  gpr_uint8 success;
} batch_control;

struct grpc_call {
  grpc_completion_queue *cq;
  grpc_channel *channel;
  grpc_call *parent;
  grpc_call *first_child;
  grpc_mdctx *metadata_context;
  /* TODO(ctiller): share with cq if possible? */
  gpr_mu mu;

  /* client or server call */
  gpr_uint8 is_client;
  /* is the alarm set */
  gpr_uint8 have_alarm;
  /** has grpc_call_destroy been called */
  gpr_uint8 destroy_called;
  /** flag indicating that cancellation is inherited */
  gpr_uint8 cancellation_is_inherited;
  /** bitmask of live batches */
  gpr_uint8 used_batches;
  /** which ops are in-flight */
  gpr_uint8 sent_initial_metadata;
  gpr_uint8 sending_message;
  gpr_uint8 sent_final_op;
  gpr_uint8 received_initial_metadata;
  gpr_uint8 receiving_message;
  gpr_uint8 received_final_op;

  batch_control active_batches[MAX_CONCURRENT_BATCHES];

  /* first idx: is_receiving, second idx: is_trailing */
  grpc_metadata_batch metadata_batch[2][2];

  /* Buffered read metadata waiting to be returned to the application.
     Element 0 is initial metadata, element 1 is trailing metadata. */
  grpc_metadata_array *buffered_metadata[2];

  /* Received call statuses from various sources */
  received_status status[STATUS_SOURCE_COUNT];

  /* Compression algorithm for the call */
  grpc_compression_algorithm compression_algorithm;
  /* Supported encodings (compression algorithms), a bitset */
  gpr_uint32 encodings_accepted_by_peer;

  /* Contexts for various subsystems (security, tracing, ...). */
  grpc_call_context_element context[GRPC_CONTEXT_COUNT];

  /* Deadline alarm - if have_alarm is non-zero */
  grpc_timer alarm;

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

  grpc_slice_buffer_stream sending_stream;
  grpc_byte_stream *receiving_stream;
  grpc_byte_buffer **receiving_buffer;
  gpr_slice receiving_slice;
  grpc_closure receiving_slice_ready;
  grpc_closure receiving_stream_ready;
  gpr_uint32 test_only_last_message_flags;

  union {
    struct {
      grpc_status_code *status;
      char **status_details;
      size_t *status_details_capacity;
    } client;
    struct {
      int *cancelled;
    } server;
  } final_op;
};

#define CALL_STACK_FROM_CALL(call) ((grpc_call_stack *)((call) + 1))
#define CALL_FROM_CALL_STACK(call_stack) (((grpc_call *)(call_stack)) - 1)
#define CALL_ELEM_FROM_CALL(call, idx) \
  grpc_call_stack_element(CALL_STACK_FROM_CALL(call), idx)
#define CALL_FROM_TOP_ELEM(top_elem) \
  CALL_FROM_CALL_STACK(grpc_call_stack_from_top_element(top_elem))

static void set_deadline_alarm(grpc_exec_ctx *exec_ctx, grpc_call *call,
                               gpr_timespec deadline);
static void execute_op(grpc_exec_ctx *exec_ctx, grpc_call *call,
                       grpc_transport_stream_op *op);
static grpc_call_error cancel_with_status(grpc_exec_ctx *exec_ctx, grpc_call *c,
                                          grpc_status_code status,
                                          const char *description);
static void destroy_call(grpc_exec_ctx *exec_ctx, void *call_stack,
                         int success);
static void receiving_slice_ready(grpc_exec_ctx *exec_ctx, void *bctlp,
                                  int success);

grpc_call *grpc_call_create(grpc_channel *channel, grpc_call *parent_call,
                            gpr_uint32 propagation_mask,
                            grpc_completion_queue *cq,
                            const void *server_transport_data,
                            grpc_mdelem **add_initial_metadata,
                            size_t add_initial_metadata_count,
                            gpr_timespec send_deadline) {
  size_t i, j;
  grpc_channel_stack *channel_stack = grpc_channel_get_channel_stack(channel);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call *call;
  GPR_TIMER_BEGIN("grpc_call_create", 0);
  call = gpr_malloc(sizeof(grpc_call) + channel_stack->call_stack_size);
  memset(call, 0, sizeof(grpc_call));
  gpr_mu_init(&call->mu);
  call->channel = channel;
  call->cq = cq;
  call->parent = parent_call;
  call->is_client = server_transport_data == NULL;
  if (call->is_client) {
    GPR_ASSERT(add_initial_metadata_count < MAX_SEND_EXTRA_METADATA_COUNT);
    for (i = 0; i < add_initial_metadata_count; i++) {
      call->send_extra_metadata[i].md = add_initial_metadata[i];
    }
    call->send_extra_metadata_count = (int)add_initial_metadata_count;
  } else {
    GPR_ASSERT(add_initial_metadata_count == 0);
    call->send_extra_metadata_count = 0;
  }
  for (i = 0; i < 2; i++) {
    for (j = 0; j < 2; j++) {
      call->metadata_batch[i][j].deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
    }
  }
  call->send_deadline = send_deadline;
  GRPC_CHANNEL_INTERNAL_REF(channel, "call");
  call->metadata_context = grpc_channel_get_metadata_context(channel);
  /* initial refcount dropped by grpc_call_destroy */
  grpc_call_stack_init(&exec_ctx, channel_stack, 1, destroy_call, call,
                       call->context, server_transport_data,
                       CALL_STACK_FROM_CALL(call));
  if (cq != NULL) {
    GRPC_CQ_INTERNAL_REF(cq, "bind");
    grpc_call_stack_set_pollset(&exec_ctx, CALL_STACK_FROM_CALL(call),
                                grpc_cq_pollset(cq));
  }
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
  GPR_ASSERT(cq);
  call->cq = cq;
  GRPC_CQ_INTERNAL_REF(cq, "bind");
  grpc_call_stack_set_pollset(exec_ctx, CALL_STACK_FROM_CALL(call),
                              grpc_cq_pollset(cq));
}

grpc_completion_queue *grpc_call_get_completion_queue(grpc_call *call) {
  return call->cq;
}

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_call_internal_ref(grpc_call *c, const char *reason) {
  grpc_call_stack_ref(CALL_STACK_FROM_CALL(c), reason);
}
void grpc_call_internal_unref(grpc_exec_ctx *exec_ctx, grpc_call *c,
                              const char *reason) {
  grpc_call_stack_unref(exec_ctx, CALL_STACK_FROM_CALL(c), reason);
}
#else
void grpc_call_internal_ref(grpc_call *c) {
  grpc_call_stack_ref(CALL_STACK_FROM_CALL(c));
}
void grpc_call_internal_unref(grpc_exec_ctx *exec_ctx, grpc_call *c) {
  grpc_call_stack_unref(exec_ctx, CALL_STACK_FROM_CALL(c));
}
#endif

static void destroy_call(grpc_exec_ctx *exec_ctx, void *call, int success) {
  size_t i;
  int ii;
  grpc_call *c = call;
  GPR_TIMER_BEGIN("destroy_call", 0);
  for (i = 0; i < 2; i++) {
    grpc_metadata_batch_destroy(
        &c->metadata_batch[1 /* is_receiving */][i /* is_initial */]);
  }
  if (c->receiving_stream != NULL) {
    grpc_byte_stream_destroy(c->receiving_stream);
  }
  grpc_call_stack_destroy(exec_ctx, CALL_STACK_FROM_CALL(c));
  GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, c->channel, "call");
  gpr_mu_destroy(&c->mu);
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (c->status[i].details) {
      GRPC_MDSTR_UNREF(c->status[i].details);
    }
  }
  for (ii = 0; ii < c->send_extra_metadata_count; ii++) {
    GRPC_MDELEM_UNREF(c->send_extra_metadata[ii].md);
  }
  for (i = 0; i < GRPC_CONTEXT_COUNT; i++) {
    if (c->context[i].destroy) {
      c->context[i].destroy(c->context[i].value);
    }
  }
  if (c->cq) {
    GRPC_CQ_INTERNAL_UNREF(c->cq, "bind");
  }
  gpr_free(c);
  GPR_TIMER_END("destroy_call", 0);
}

static void set_status_code(grpc_call *call, status_source source,
                            gpr_uint32 status) {
  if (call->status[source].is_set) return;

  call->status[source].is_set = 1;
  call->status[source].code = (grpc_status_code)status;

  /* TODO(ctiller): what to do about the flush that was previously here */
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

gpr_uint32 grpc_call_test_only_get_message_flags(grpc_call *call) {
  gpr_uint32 flags;
  gpr_mu_lock(&call->mu);
  flags = call->test_only_last_message_flags;
  gpr_mu_unlock(&call->mu);
  return flags;
}

static void destroy_encodings_accepted_by_peer(void *p) { return; }

static void set_encodings_accepted_by_peer(grpc_call *call, grpc_mdelem *mdel) {
  size_t i;
  grpc_compression_algorithm algorithm;
  gpr_slice_buffer accept_encoding_parts;
  gpr_slice accept_encoding_slice;
  void *accepted_user_data;

  accepted_user_data =
      grpc_mdelem_get_user_data(mdel, destroy_encodings_accepted_by_peer);
  if (accepted_user_data != NULL) {
    call->encodings_accepted_by_peer =
        (gpr_uint32)(((gpr_uintptr)accepted_user_data) - 1);
    return;
  }

  accept_encoding_slice = mdel->value->slice;
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

  gpr_slice_buffer_destroy(&accept_encoding_parts);

  grpc_mdelem_set_user_data(
      mdel, destroy_encodings_accepted_by_peer,
      (void *)(((gpr_uintptr)call->encodings_accepted_by_peer) + 1));
}

gpr_uint32 grpc_call_test_only_get_encodings_accepted_by_peer(grpc_call *call) {
  gpr_uint32 encodings_accepted_by_peer;
  gpr_mu_lock(&call->mu);
  encodings_accepted_by_peer = call->encodings_accepted_by_peer;
  gpr_mu_unlock(&call->mu);
  return encodings_accepted_by_peer;
}

static void set_status_details(grpc_call *call, status_source source,
                               grpc_mdstr *status) {
  if (call->status[source].details != NULL) {
    GRPC_MDSTR_UNREF(call->status[source].details);
  }
  call->status[source].details = status;
}

static void get_final_status(grpc_call *call,
                             void (*set_value)(grpc_status_code code,
                                               void *user_data),
                             void *set_value_user_data) {
  int i;
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (call->status[i].is_set) {
      set_value(call->status[i].code, set_value_user_data);
      return;
    }
  }
  if (call->is_client) {
    set_value(GRPC_STATUS_UNKNOWN, set_value_user_data);
  } else {
    set_value(GRPC_STATUS_OK, set_value_user_data);
  }
}

static void get_final_details(grpc_call *call, char **out_details,
                              size_t *out_details_capacity) {
  int i;
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (call->status[i].is_set) {
      if (call->status[i].details) {
        gpr_slice details = call->status[i].details->slice;
        size_t len = GPR_SLICE_LENGTH(details);
        if (len + 1 > *out_details_capacity) {
          *out_details_capacity =
              GPR_MAX(len + 1, *out_details_capacity * 3 / 2);
          *out_details = gpr_realloc(*out_details, *out_details_capacity);
        }
        memcpy(*out_details, GPR_SLICE_START_PTR(details), len);
        (*out_details)[len] = 0;
      } else {
        goto no_details;
      }
      return;
    }
  }

no_details:
  if (0 == *out_details_capacity) {
    *out_details_capacity = 8;
    *out_details = gpr_malloc(*out_details_capacity);
  }
  **out_details = 0;
}

static grpc_linked_mdelem *linked_from_md(grpc_metadata *md) {
  return (grpc_linked_mdelem *)&md->internal_data;
}

static int prepare_application_metadata(grpc_call *call, int count,
                                        grpc_metadata *metadata,
                                        int is_trailing,
                                        int prepend_extra_metadata) {
  int i;
  grpc_metadata_batch *batch =
      &call->metadata_batch[0 /* is_receiving */][is_trailing];
  if (prepend_extra_metadata) {
    if (call->send_extra_metadata_count == 0) {
      prepend_extra_metadata = 0;
    } else {
      for (i = 0; i < call->send_extra_metadata_count; i++) {
        GRPC_MDELEM_REF(call->send_extra_metadata[i].md);
      }
      for (i = 1; i < call->send_extra_metadata_count; i++) {
        call->send_extra_metadata[i].prev = &call->send_extra_metadata[i - 1];
      }
      for (i = 0; i < call->send_extra_metadata_count - 1; i++) {
        call->send_extra_metadata[i].next = &call->send_extra_metadata[i + 1];
      }
    }
  }
  for (i = 0; i < count; i++) {
    grpc_metadata *md = &metadata[i];
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
  }
  for (i = 1; i < count; i++) {
    linked_from_md(&metadata[i])->prev = linked_from_md(&metadata[i - 1]);
  }
  for (i = 0; i < count - 1; i++) {
    linked_from_md(&metadata[i])->next = linked_from_md(&metadata[i + 1]);
  }
  switch (prepend_extra_metadata * 2 + (count != 0)) {
    case 0:
      /* no prepend, no metadata => nothing to do */
      batch->list.head = batch->list.tail = NULL;
      break;
    case 1:
      /* metadata, but no prepend */
      batch->list.head = linked_from_md(&metadata[0]);
      batch->list.tail = linked_from_md(&metadata[count - 1]);
      batch->list.head->prev = NULL;
      batch->list.tail->next = NULL;
      break;
    case 2:
      /* prepend, but no md */
      batch->list.head = &call->send_extra_metadata[0];
      batch->list.tail =
          &call->send_extra_metadata[call->send_extra_metadata_count - 1];
      batch->list.head->prev = NULL;
      batch->list.tail->next = NULL;
      break;
    case 3:
      /* prepend AND md */
      batch->list.head = &call->send_extra_metadata[0];
      call->send_extra_metadata[call->send_extra_metadata_count - 1].next =
          linked_from_md(&metadata[0]);
      linked_from_md(&metadata[0])->prev =
          &call->send_extra_metadata[call->send_extra_metadata_count - 1];
      batch->list.tail = linked_from_md(&metadata[count - 1]);
      batch->list.head->prev = NULL;
      batch->list.tail->next = NULL;
      break;
    default:
      GPR_UNREACHABLE_CODE(return 0);
  }

  return 1;
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
  if (c->have_alarm) {
    grpc_timer_cancel(&exec_ctx, &c->alarm);
  }
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

typedef struct cancel_closure {
  grpc_closure closure;
  grpc_call *call;
  grpc_status_code status;
} cancel_closure;

static void done_cancel(grpc_exec_ctx *exec_ctx, void *ccp, int success) {
  cancel_closure *cc = ccp;
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, cc->call, "cancel");
  gpr_free(cc);
}

static void send_cancel(grpc_exec_ctx *exec_ctx, void *ccp, int success) {
  grpc_transport_stream_op op;
  cancel_closure *cc = ccp;
  memset(&op, 0, sizeof(op));
  op.cancel_with_status = cc->status;
  /* reuse closure to catch completion */
  grpc_closure_init(&cc->closure, done_cancel, cc);
  op.on_complete = &cc->closure;
  execute_op(exec_ctx, cc->call, &op);
}

static grpc_call_error cancel_with_status(grpc_exec_ctx *exec_ctx, grpc_call *c,
                                          grpc_status_code status,
                                          const char *description) {
  grpc_mdstr *details =
      description ? grpc_mdstr_from_string(c->metadata_context, description)
                  : NULL;
  cancel_closure *cc = gpr_malloc(sizeof(*cc));

  GPR_ASSERT(status != GRPC_STATUS_OK);

  set_status_code(c, STATUS_FROM_API_OVERRIDE, (gpr_uint32)status);
  set_status_details(c, STATUS_FROM_API_OVERRIDE, details);

  grpc_closure_init(&cc->closure, send_cancel, cc);
  cc->call = c;
  cc->status = status;
  GRPC_CALL_INTERNAL_REF(c, "cancel");
  grpc_exec_ctx_enqueue(exec_ctx, &cc->closure, 1);

  return GRPC_CALL_OK;
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
  gpr_mu_lock(&call->mu);
  call->have_alarm = 0;
  if (success) {
    cancel_with_status(exec_ctx, call, GRPC_STATUS_DEADLINE_EXCEEDED,
                       "Deadline Exceeded");
  }
  gpr_mu_unlock(&call->mu);
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
  grpc_timer_init(exec_ctx, &call->alarm, call->send_deadline, call_alarm, call,
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

static grpc_mdelem *recv_common_filter(grpc_call *call, grpc_mdelem *elem) {
  if (elem->key == grpc_channel_get_status_string(call->channel)) {
    GPR_TIMER_BEGIN("status", 0);
    set_status_code(call, STATUS_FROM_WIRE, decode_status(elem));
    GPR_TIMER_END("status", 0);
    return NULL;
  } else if (elem->key == grpc_channel_get_message_string(call->channel)) {
    GPR_TIMER_BEGIN("status-details", 0);
    set_status_details(call, STATUS_FROM_WIRE, GRPC_MDSTR_REF(elem->value));
    GPR_TIMER_END("status-details", 0);
    return NULL;
  }
  return elem;
}

static grpc_mdelem *publish_app_metadata(grpc_call *call, grpc_mdelem *elem,
                                         int is_trailing) {
  grpc_metadata_array *dest;
  grpc_metadata *mdusr;
  GPR_TIMER_BEGIN("publish_app_metadata", 0);
  dest = call->buffered_metadata[is_trailing];
  if (dest->count == dest->capacity) {
    dest->capacity = GPR_MAX(dest->capacity + 8, dest->capacity * 2);
    dest->metadata =
        gpr_realloc(dest->metadata, sizeof(grpc_metadata) * dest->capacity);
  }
  mdusr = &dest->metadata[dest->count++];
  mdusr->key = grpc_mdstr_as_c_string(elem->key);
  mdusr->value = grpc_mdstr_as_c_string(elem->value);
  mdusr->value_length = GPR_SLICE_LENGTH(elem->value->slice);
  GPR_TIMER_END("publish_app_metadata", 0);
  return elem;
}

static grpc_mdelem *recv_initial_filter(void *callp, grpc_mdelem *elem) {
  grpc_call *call = callp;
  elem = recv_common_filter(call, elem);
  if (elem == NULL) {
    return NULL;
  } else if (elem->key ==
             grpc_channel_get_compression_algorithm_string(call->channel)) {
    GPR_TIMER_BEGIN("compression_algorithm", 0);
    set_compression_algorithm(call, decode_compression(elem));
    GPR_TIMER_END("compression_algorithm", 0);
    return NULL;
  } else if (elem->key == grpc_channel_get_encodings_accepted_by_peer_string(
                              call->channel)) {
    GPR_TIMER_BEGIN("encodings_accepted_by_peer", 0);
    set_encodings_accepted_by_peer(call, elem);
    GPR_TIMER_END("encodings_accepted_by_peer", 0);
    return NULL;
  } else {
    return publish_app_metadata(call, elem, 0);
  }
}

static grpc_mdelem *recv_trailing_filter(void *callp, grpc_mdelem *elem) {
  grpc_call *call = callp;
  elem = recv_common_filter(call, elem);
  if (elem == NULL) {
    return NULL;
  } else {
    return publish_app_metadata(call, elem, 1);
  }
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
  *(int *)dest = (status != GRPC_STATUS_OK);
}

static int are_write_flags_valid(gpr_uint32 flags) {
  /* check that only bits in GRPC_WRITE_(INTERNAL?)_USED_MASK are set */
  const gpr_uint32 allowed_write_positions =
      (GRPC_WRITE_USED_MASK | GRPC_WRITE_INTERNAL_USED_MASK);
  const gpr_uint32 invalid_positions = ~allowed_write_positions;
  return !(flags & invalid_positions);
}

static batch_control *allocate_batch_control(grpc_call *call) {
  size_t i;
  for (i = 0; i < MAX_CONCURRENT_BATCHES; i++) {
    if ((call->used_batches & (1 << i)) == 0) {
      call->used_batches |= (gpr_uint8)(1 << i);
      return &call->active_batches[i];
    }
  }
  GPR_UNREACHABLE_CODE(return NULL);
}

static void finish_batch_completion(grpc_exec_ctx *exec_ctx, void *user_data,
                                    grpc_cq_completion *storage) {
  batch_control *bctl = user_data;
  grpc_call *call = bctl->call;
  gpr_mu_lock(&call->mu);
  call->used_batches = (gpr_uint8)(
      call->used_batches & ~(gpr_uint8)(1 << (bctl - call->active_batches)));
  gpr_mu_unlock(&call->mu);
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "completion");
}

static void post_batch_completion(grpc_exec_ctx *exec_ctx,
                                  batch_control *bctl) {
  grpc_call *call = bctl->call;
  if (bctl->is_notify_tag_closure) {
    grpc_exec_ctx_enqueue(exec_ctx, bctl->notify_tag, bctl->success);
    gpr_mu_lock(&call->mu);
    bctl->call->used_batches =
        (gpr_uint8)(bctl->call->used_batches &
                    ~(gpr_uint8)(1 << (bctl - bctl->call->active_batches)));
    gpr_mu_unlock(&call->mu);
    GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "completion");
  } else {
    grpc_cq_end_op(exec_ctx, bctl->call->cq, bctl->notify_tag, bctl->success,
                   finish_batch_completion, bctl, &bctl->cq_completion);
  }
}

static void continue_receiving_slices(grpc_exec_ctx *exec_ctx,
                                      batch_control *bctl) {
  grpc_call *call = bctl->call;
  for (;;) {
    size_t remaining = call->receiving_stream->length -
                       (*call->receiving_buffer)->data.raw.slice_buffer.length;
    if (remaining == 0) {
      call->receiving_message = 0;
      grpc_byte_stream_destroy(call->receiving_stream);
      call->receiving_stream = NULL;
      if (gpr_unref(&bctl->steps_to_complete)) {
        post_batch_completion(exec_ctx, bctl);
      }
      return;
    }
    if (grpc_byte_stream_next(exec_ctx, call->receiving_stream,
                              &call->receiving_slice, remaining,
                              &call->receiving_slice_ready)) {
      gpr_slice_buffer_add(&(*call->receiving_buffer)->data.raw.slice_buffer,
                           call->receiving_slice);
    } else {
      return;
    }
  }
}

static void receiving_slice_ready(grpc_exec_ctx *exec_ctx, void *bctlp,
                                  int success) {
  batch_control *bctl = bctlp;
  grpc_call *call = bctl->call;

  GPR_ASSERT(success);
  gpr_slice_buffer_add(&(*call->receiving_buffer)->data.raw.slice_buffer,
                       call->receiving_slice);

  continue_receiving_slices(exec_ctx, bctl);
}

static void finish_batch(grpc_exec_ctx *exec_ctx, void *bctlp, int success) {
  batch_control *bctl = bctlp;
  grpc_call *call = bctl->call;
  grpc_call *child_call;
  grpc_call *next_child_call;

  gpr_mu_lock(&call->mu);
  if (bctl->send_initial_metadata) {
    grpc_metadata_batch_destroy(
        &call->metadata_batch[0 /* is_receiving */][0 /* is_trailing */]);
  }
  if (bctl->send_message) {
    call->sending_message = 0;
  }
  if (bctl->send_final_op) {
    grpc_metadata_batch_destroy(
        &call->metadata_batch[0 /* is_receiving */][1 /* is_trailing */]);
  }
  if (bctl->recv_initial_metadata) {
    grpc_metadata_batch *md =
        &call->metadata_batch[1 /* is_receiving */][0 /* is_trailing */];
    grpc_metadata_batch_filter(md, recv_initial_filter, call);

    if (gpr_time_cmp(md->deadline, gpr_inf_future(md->deadline.clock_type)) !=
            0 &&
        !call->is_client) {
      GPR_TIMER_BEGIN("set_deadline_alarm", 0);
      set_deadline_alarm(exec_ctx, call, md->deadline);
      GPR_TIMER_END("set_deadline_alarm", 0);
    }
  }
  if (bctl->recv_final_op) {
    grpc_metadata_batch *md =
        &call->metadata_batch[1 /* is_receiving */][1 /* is_trailing */];
    grpc_metadata_batch_filter(md, recv_trailing_filter, call);

    if (call->have_alarm) {
      grpc_timer_cancel(exec_ctx, &call->alarm);
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

    if (call->is_client) {
      get_final_status(call, set_status_value_directly,
                       call->final_op.client.status);
      get_final_details(call, call->final_op.client.status_details,
                        call->final_op.client.status_details_capacity);
    } else {
      get_final_status(call, set_cancelled_value,
                       call->final_op.server.cancelled);
    }

    success = 1;
  }
  bctl->success = success != 0;
  gpr_mu_unlock(&call->mu);
  if (gpr_unref(&bctl->steps_to_complete)) {
    post_batch_completion(exec_ctx, bctl);
  }
}

static void receiving_stream_ready(grpc_exec_ctx *exec_ctx, void *bctlp,
                                   int success) {
  batch_control *bctl = bctlp;
  grpc_call *call = bctl->call;

  if (call->receiving_stream == NULL) {
    *call->receiving_buffer = NULL;
    if (gpr_unref(&bctl->steps_to_complete)) {
      post_batch_completion(exec_ctx, bctl);
    }
  } else if (call->receiving_stream->length >
             grpc_channel_get_max_message_length(call->channel)) {
    cancel_with_status(exec_ctx, call, GRPC_STATUS_INTERNAL,
                       "Max message size exceeded");
    grpc_byte_stream_destroy(call->receiving_stream);
    call->receiving_stream = NULL;
    *call->receiving_buffer = NULL;
    if (gpr_unref(&bctl->steps_to_complete)) {
      post_batch_completion(exec_ctx, bctl);
    }
  } else {
    call->test_only_last_message_flags = call->receiving_stream->flags;
    if ((call->receiving_stream->flags & GRPC_WRITE_INTERNAL_COMPRESS) &&
        (call->compression_algorithm > GRPC_COMPRESS_NONE)) {
      *call->receiving_buffer = grpc_raw_compressed_byte_buffer_create(
          NULL, 0, call->compression_algorithm);
    } else {
      *call->receiving_buffer = grpc_raw_byte_buffer_create(NULL, 0);
    }
    grpc_closure_init(&call->receiving_slice_ready, receiving_slice_ready,
                      bctl);
    continue_receiving_slices(exec_ctx, bctl);
    /* early out */
    return;
  }
}

static grpc_call_error call_start_batch(grpc_exec_ctx *exec_ctx,
                                        grpc_call *call, const grpc_op *ops,
                                        size_t nops, void *notify_tag,
                                        int is_notify_tag_closure) {
  grpc_transport_stream_op stream_op;
  size_t i;
  const grpc_op *op;
  batch_control *bctl;
  int num_completion_callbacks_needed = 1;
  grpc_call_error error = GRPC_CALL_OK;

  GPR_TIMER_BEGIN("grpc_call_start_batch", 0);

  GRPC_CALL_LOG_BATCH(GPR_INFO, call, ops, nops, notify_tag);

  memset(&stream_op, 0, sizeof(stream_op));

  /* TODO(ctiller): this feels like it could be made lock-free */
  gpr_mu_lock(&call->mu);
  bctl = allocate_batch_control(call);
  memset(bctl, 0, sizeof(*bctl));
  bctl->call = call;
  bctl->notify_tag = notify_tag;
  bctl->is_notify_tag_closure = (gpr_uint8)(is_notify_tag_closure != 0);

  if (nops == 0) {
    GRPC_CALL_INTERNAL_REF(call, "completion");
    bctl->success = 1;
    if (!is_notify_tag_closure) {
      grpc_cq_begin_op(call->cq);
    }
    gpr_mu_unlock(&call->mu);
    post_batch_completion(exec_ctx, bctl);
    return GRPC_CALL_OK;
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
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->sent_initial_metadata) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        if (op->data.send_initial_metadata.count > INT_MAX) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        bctl->send_initial_metadata = 1;
        call->sent_initial_metadata = 1;
        if (!prepare_application_metadata(
                call, (int)op->data.send_initial_metadata.count,
                op->data.send_initial_metadata.metadata, 0, call->is_client)) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        /* TODO(ctiller): just make these the same variable? */
        call->metadata_batch[0][0].deadline = call->send_deadline;
        stream_op.send_initial_metadata =
            &call->metadata_batch[0 /* is_receiving */][0 /* is_trailing */];
        break;
      case GRPC_OP_SEND_MESSAGE:
        if (!are_write_flags_valid(op->flags)) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (op->data.send_message == NULL) {
          error = GRPC_CALL_ERROR_INVALID_MESSAGE;
          goto done_with_error;
        }
        if (call->sending_message) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        bctl->send_message = 1;
        call->sending_message = 1;
        grpc_slice_buffer_stream_init(
            &call->sending_stream,
            &op->data.send_message->data.raw.slice_buffer, op->flags);
        stream_op.send_message = &call->sending_stream.base;
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
        stream_op.send_trailing_metadata =
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
            call->channel, op->data.send_status_from_server.status);
        if (op->data.send_status_from_server.status_details != NULL) {
          call->send_extra_metadata[1].md = grpc_mdelem_from_metadata_strings(
              call->metadata_context,
              GRPC_MDSTR_REF(grpc_channel_get_message_string(call->channel)),
              grpc_mdstr_from_string(
                  call->metadata_context,
                  op->data.send_status_from_server.status_details));
          call->send_extra_metadata_count++;
          set_status_details(
              call, STATUS_FROM_API_OVERRIDE,
              GRPC_MDSTR_REF(call->send_extra_metadata[1].md->value));
        }
        set_status_code(call, STATUS_FROM_API_OVERRIDE,
                        (gpr_uint32)op->data.send_status_from_server.status);
        if (!prepare_application_metadata(
                call,
                (int)op->data.send_status_from_server.trailing_metadata_count,
                op->data.send_status_from_server.trailing_metadata, 1, 1)) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        stream_op.send_trailing_metadata =
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
        call->received_initial_metadata = 1;
        call->buffered_metadata[0] = op->data.recv_initial_metadata;
        bctl->recv_initial_metadata = 1;
        stream_op.recv_initial_metadata =
            &call->metadata_batch[1 /* is_receiving */][0 /* is_trailing */];
        break;
      case GRPC_OP_RECV_MESSAGE:
        /* Flag validation: currently allow no flags */
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (call->receiving_message) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
        }
        call->receiving_message = 1;
        bctl->recv_message = 1;
        call->receiving_buffer = op->data.recv_message;
        stream_op.recv_message = &call->receiving_stream;
        grpc_closure_init(&call->receiving_stream_ready, receiving_stream_ready,
                          bctl);
        stream_op.recv_message_ready = &call->receiving_stream_ready;
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
        if (call->received_final_op) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        call->received_final_op = 1;
        call->buffered_metadata[1] =
            op->data.recv_status_on_client.trailing_metadata;
        call->final_op.client.status = op->data.recv_status_on_client.status;
        call->final_op.client.status_details =
            op->data.recv_status_on_client.status_details;
        call->final_op.client.status_details_capacity =
            op->data.recv_status_on_client.status_details_capacity;
        bctl->recv_final_op = 1;
        stream_op.recv_trailing_metadata =
            &call->metadata_batch[1 /* is_receiving */][1 /* is_trailing */];
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
        if (call->received_final_op) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        call->received_final_op = 1;
        call->final_op.server.cancelled =
            op->data.recv_close_on_server.cancelled;
        bctl->recv_final_op = 1;
        stream_op.recv_trailing_metadata =
            &call->metadata_batch[1 /* is_receiving */][1 /* is_trailing */];
        break;
    }
  }

  GRPC_CALL_INTERNAL_REF(call, "completion");
  if (!is_notify_tag_closure) {
    grpc_cq_begin_op(call->cq);
  }
  gpr_ref_init(&bctl->steps_to_complete, num_completion_callbacks_needed);

  stream_op.context = call->context;
  grpc_closure_init(&bctl->finish_batch, finish_batch, bctl);
  stream_op.on_complete = &bctl->finish_batch;
  gpr_mu_unlock(&call->mu);

  execute_op(exec_ctx, call, &stream_op);

done:
  GPR_TIMER_END("grpc_call_start_batch", 0);
  return error;

done_with_error:
  /* reverse any mutations that occured */
  if (bctl->send_initial_metadata) {
    call->sent_initial_metadata = 0;
    grpc_metadata_batch_clear(&call->metadata_batch[0][0]);
  }
  if (bctl->send_message) {
    call->sending_message = 0;
    grpc_byte_stream_destroy(&call->sending_stream.base);
  }
  if (bctl->send_final_op) {
    call->sent_final_op = 0;
    grpc_metadata_batch_clear(&call->metadata_batch[0][1]);
  }
  if (bctl->recv_initial_metadata) {
    call->received_initial_metadata = 0;
  }
  if (bctl->recv_message) {
    call->receiving_message = 0;
  }
  if (bctl->recv_final_op) {
    call->received_final_op = 0;
  }
  gpr_mu_unlock(&call->mu);
  goto done;
}

grpc_call_error grpc_call_start_batch(grpc_call *call, const grpc_op *ops,
                                      size_t nops, void *tag, void *reserved) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call_error err;

  GRPC_API_TRACE(
      "grpc_call_start_batch(call=%p, ops=%p, nops=%lu, tag=%p, reserved=%p)",
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
