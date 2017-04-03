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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/slice/slice_internal.h"

typedef struct call_data {
  grpc_metadata_batch *recv_initial_metadata;
  /* Closure to call when finished with the auth_on_recv hook. */
  grpc_closure *on_done_recv;
  /* Receive closures are chained: we inject this closure as the on_done_recv
     up-call on transport_op, and remember to call our on_done_recv member after
     handling it. */
  grpc_closure auth_on_recv;
  grpc_transport_stream_op_batch *transport_op;
  grpc_metadata_array md;
  const grpc_metadata *consumed_md;
  size_t num_consumed_md;
  grpc_auth_context *auth_context;
} call_data;

typedef struct channel_data {
  grpc_auth_context *auth_context;
  grpc_server_credentials *creds;
} channel_data;

static grpc_metadata_array metadata_batch_to_md_array(
    const grpc_metadata_batch *batch) {
  grpc_linked_mdelem *l;
  grpc_metadata_array result;
  grpc_metadata_array_init(&result);
  for (l = batch->list.head; l != NULL; l = l->next) {
    grpc_metadata *usr_md = NULL;
    grpc_mdelem md = l->md;
    grpc_slice key = GRPC_MDKEY(md);
    grpc_slice value = GRPC_MDVALUE(md);
    if (result.count == result.capacity) {
      result.capacity = GPR_MAX(result.capacity + 8, result.capacity * 2);
      result.metadata =
          gpr_realloc(result.metadata, result.capacity * sizeof(grpc_metadata));
    }
    usr_md = &result.metadata[result.count++];
    usr_md->key = grpc_slice_ref_internal(key);
    usr_md->value = grpc_slice_ref_internal(value);
  }
  return result;
}

static grpc_filtered_mdelem remove_consumed_md(grpc_exec_ctx *exec_ctx,
                                               void *user_data,
                                               grpc_mdelem md) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  size_t i;
  for (i = 0; i < calld->num_consumed_md; i++) {
    const grpc_metadata *consumed_md = &calld->consumed_md[i];
    if (grpc_slice_eq(GRPC_MDKEY(md), consumed_md->key) &&
        grpc_slice_eq(GRPC_MDVALUE(md), consumed_md->value))
      return GRPC_FILTERED_REMOVE();
  }
  return GRPC_FILTERED_MDELEM(md);
}

/* called from application code */
static void on_md_processing_done(
    void *user_data, const grpc_metadata *consumed_md, size_t num_consumed_md,
    const grpc_metadata *response_md, size_t num_response_md,
    grpc_status_code status, const char *error_details) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  /* TODO(jboeuf): Implement support for response_md. */
  if (response_md != NULL && num_response_md > 0) {
    gpr_log(GPR_INFO,
            "response_md in auth metadata processing not supported for now. "
            "Ignoring...");
  }

  if (status == GRPC_STATUS_OK) {
    calld->consumed_md = consumed_md;
    calld->num_consumed_md = num_consumed_md;
    /* TODO(ctiller): propagate error */
    GRPC_LOG_IF_ERROR(
        "grpc_metadata_batch_filter",
        grpc_metadata_batch_filter(&exec_ctx, calld->recv_initial_metadata,
                                   remove_consumed_md, elem,
                                   "Response metadata filtering error"));
    for (size_t i = 0; i < calld->md.count; i++) {
      grpc_slice_unref_internal(&exec_ctx, calld->md.metadata[i].key);
      grpc_slice_unref_internal(&exec_ctx, calld->md.metadata[i].value);
    }
    grpc_metadata_array_destroy(&calld->md);
    grpc_closure_sched(&exec_ctx, calld->on_done_recv, GRPC_ERROR_NONE);
  } else {
    for (size_t i = 0; i < calld->md.count; i++) {
      grpc_slice_unref_internal(&exec_ctx, calld->md.metadata[i].key);
      grpc_slice_unref_internal(&exec_ctx, calld->md.metadata[i].value);
    }
    grpc_metadata_array_destroy(&calld->md);
    error_details = error_details != NULL
                        ? error_details
                        : "Authentication metadata processing failed.";
    if (calld->transport_op->send_message) {
      grpc_byte_stream_destroy(
          &exec_ctx, calld->transport_op->payload->send_message.send_message);
      calld->transport_op->payload->send_message.send_message = NULL;
    }
    grpc_closure_sched(
        &exec_ctx, calld->on_done_recv,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_details),
                           GRPC_ERROR_INT_GRPC_STATUS, status));
  }

  grpc_exec_ctx_finish(&exec_ctx);
}

static void auth_on_recv(grpc_exec_ctx *exec_ctx, void *user_data,
                         grpc_error *error) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (error == GRPC_ERROR_NONE) {
    if (chand->creds != NULL && chand->creds->processor.process != NULL) {
      calld->md = metadata_batch_to_md_array(calld->recv_initial_metadata);
      chand->creds->processor.process(
          chand->creds->processor.state, calld->auth_context,
          calld->md.metadata, calld->md.count, on_md_processing_done, elem);
      return;
    }
  }
  grpc_closure_sched(exec_ctx, calld->on_done_recv, GRPC_ERROR_REF(error));
}

static void set_recv_ops_md_callbacks(grpc_call_element *elem,
                                      grpc_transport_stream_op_batch *op) {
  call_data *calld = elem->call_data;

  if (op->recv_initial_metadata) {
    /* substitute our callback for the higher callback */
    calld->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->on_done_recv =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->auth_on_recv;
    calld->transport_op = op;
  }
}

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void auth_start_transport_op(grpc_exec_ctx *exec_ctx,
                                    grpc_call_element *elem,
                                    grpc_transport_stream_op_batch *op) {
  set_recv_ops_md_callbacks(elem, op);
  grpc_call_next_op(exec_ctx, elem, op);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_server_security_context *server_ctx = NULL;

  /* initialize members */
  memset(calld, 0, sizeof(*calld));
  grpc_closure_init(&calld->auth_on_recv, auth_on_recv, elem,
                    grpc_schedule_on_exec_ctx);

  if (args->context[GRPC_CONTEXT_SECURITY].value != NULL) {
    args->context[GRPC_CONTEXT_SECURITY].destroy(
        args->context[GRPC_CONTEXT_SECURITY].value);
  }

  server_ctx = grpc_server_security_context_create();
  server_ctx->auth_context = grpc_auth_context_create(chand->auth_context);
  calld->auth_context = server_ctx->auth_context;

  args->context[GRPC_CONTEXT_SECURITY].value = server_ctx;
  args->context[GRPC_CONTEXT_SECURITY].destroy =
      grpc_server_security_context_destroy;

  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {}

/* Constructor for channel_data */
static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  grpc_auth_context *auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  grpc_server_credentials *creds =
      grpc_find_server_credentials_in_args(args->channel_args);
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;

  GPR_ASSERT(!args->is_last);
  GPR_ASSERT(auth_context != NULL);

  /* initialize members */
  chand->auth_context =
      GRPC_AUTH_CONTEXT_REF(auth_context, "server_auth_filter");
  chand->creds = grpc_server_credentials_ref(creds);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;
  GRPC_AUTH_CONTEXT_UNREF(chand->auth_context, "server_auth_filter");
  grpc_server_credentials_unref(exec_ctx, chand->creds);
}

const grpc_channel_filter grpc_server_auth_filter = {
    auth_start_transport_op,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "server-auth"};
