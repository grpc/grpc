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

#include "src/core/lib/surface/lame_client.h"

#include <grpc/grpc.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/static_metadata.h"

typedef struct {
  grpc_linked_mdelem status;
  grpc_linked_mdelem details;
  gpr_atm filled_metadata;
} call_data;

typedef struct {
  grpc_status_code error_code;
  const char *error_message;
} channel_data;

static void fill_metadata(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                          grpc_metadata_batch *mdb) {
  call_data *calld = elem->call_data;
  if (!gpr_atm_no_barrier_cas(&calld->filled_metadata, 0, 1)) {
    return;
  }
  channel_data *chand = elem->channel_data;
  char tmp[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(chand->error_code, tmp);
  calld->status.md = grpc_mdelem_from_slices(
      exec_ctx, GRPC_MDSTR_GRPC_STATUS, grpc_slice_from_copied_string(tmp));
  calld->details.md = grpc_mdelem_from_slices(
      exec_ctx, GRPC_MDSTR_GRPC_MESSAGE,
      grpc_slice_from_copied_string(chand->error_message));
  calld->status.prev = calld->details.next = NULL;
  calld->status.next = &calld->details;
  calld->details.prev = &calld->status;
  mdb->list.head = &calld->status;
  mdb->list.tail = &calld->details;
  mdb->list.count = 2;
  mdb->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
}

static void lame_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                           grpc_call_element *elem,
                                           grpc_transport_stream_op *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  if (op->recv_initial_metadata != NULL) {
    fill_metadata(exec_ctx, elem, op->recv_initial_metadata);
  } else if (op->recv_trailing_metadata != NULL) {
    fill_metadata(exec_ctx, elem, op->recv_trailing_metadata);
  }
  grpc_transport_stream_op_finish_with_failure(
      exec_ctx, op, GRPC_ERROR_CREATE("lame client channel"));
}

static char *lame_get_peer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  return NULL;
}

static void lame_get_channel_info(grpc_exec_ctx *exec_ctx,
                                  grpc_channel_element *elem,
                                  const grpc_channel_info *channel_info) {}

static void lame_start_transport_op(grpc_exec_ctx *exec_ctx,
                                    grpc_channel_element *elem,
                                    grpc_transport_op *op) {
  if (op->on_connectivity_state_change) {
    GPR_ASSERT(*op->connectivity_state != GRPC_CHANNEL_SHUTDOWN);
    *op->connectivity_state = GRPC_CHANNEL_SHUTDOWN;
    grpc_closure_sched(exec_ctx, op->on_connectivity_state_change,
                       GRPC_ERROR_NONE);
  }
  if (op->send_ping != NULL) {
    grpc_closure_sched(exec_ctx, op->send_ping,
                       GRPC_ERROR_CREATE("lame client channel"));
  }
  GRPC_ERROR_UNREF(op->disconnect_with_error);
  if (op->on_consumed != NULL) {
    grpc_closure_sched(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);
  }
}

static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  gpr_atm_no_barrier_store(&calld->filled_metadata, 0);
  return GRPC_ERROR_NONE;
}

static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              void *and_free_memory) {
  gpr_free(and_free_memory);
}

static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  GPR_ASSERT(args->is_first);
  GPR_ASSERT(args->is_last);
  return GRPC_ERROR_NONE;
}

static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {}

const grpc_channel_filter grpc_lame_filter = {
    lame_start_transport_stream_op,
    lame_start_transport_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    lame_get_peer,
    lame_get_channel_info,
    "lame-client",
};

#define CHANNEL_STACK_FROM_CHANNEL(c) ((grpc_channel_stack *)((c) + 1))

grpc_channel *grpc_lame_client_channel_create(const char *target,
                                              grpc_status_code error_code,
                                              const char *error_message) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_channel_element *elem;
  channel_data *chand;
  grpc_channel *channel = grpc_channel_create(&exec_ctx, target, NULL,
                                              GRPC_CLIENT_LAME_CHANNEL, NULL);
  elem = grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  GRPC_API_TRACE(
      "grpc_lame_client_channel_create(target=%s, error_code=%d, "
      "error_message=%s)",
      3, (target, (int)error_code, error_message));
  GPR_ASSERT(elem->filter == &grpc_lame_filter);
  chand = (channel_data *)elem->channel_data;
  chand->error_code = error_code;
  chand->error_message = error_message;
  grpc_exec_ctx_finish(&exec_ctx);
  return channel;
}
