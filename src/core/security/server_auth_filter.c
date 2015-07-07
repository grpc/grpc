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

#include "src/core/security/auth_filters.h"
#include "src/core/security/security_connector.h"
#include "src/core/security/security_context.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

typedef struct call_data {
  gpr_uint8 got_client_metadata;
  gpr_uint8 sent_status;
  gpr_uint8 success;
  grpc_linked_mdelem status;
  grpc_stream_op_buffer *recv_ops;
  /* Closure to call when finished with the hs_on_recv hook. */
  grpc_iomgr_closure *on_done_recv;
  /* Receive closures are chained: we inject this closure as the on_done_recv
     up-call on transport_op, and remember to call our on_done_recv member after
     handling it. */
  grpc_iomgr_closure auth_on_recv;
  const grpc_metadata *consumed_md;
  size_t num_consumed_md;
  grpc_stream_op *md_op;
  grpc_auth_context **call_auth_context;
  grpc_auth_ticket ticket;
} call_data;

typedef struct channel_data {
  grpc_security_connector *security_connector;
  grpc_mdelem *status_auth_failure;
} channel_data;

static grpc_metadata_array metadata_batch_to_md_array(
    const grpc_metadata_batch *batch) {
  grpc_linked_mdelem *l;
  grpc_metadata_array result;
  grpc_metadata_array_init(&result);
  for (l = batch->list.head; l != NULL; l = l->next) {
    grpc_metadata *usr_md = NULL;
    grpc_mdelem *md = l->md;
    grpc_mdstr *key = md->key;
    grpc_mdstr *value = md->value;
    if (result.count == result.capacity) {
      result.capacity = GPR_MAX(result.capacity + 8, result.capacity * 2);
      result.metadata =
          gpr_realloc(result.metadata, result.capacity * sizeof(grpc_metadata));
    }
    usr_md = &result.metadata[result.count++];
    usr_md->key = grpc_mdstr_as_c_string(key);
    usr_md->value = grpc_mdstr_as_c_string(value);
    usr_md->value_length = GPR_SLICE_LENGTH(value->slice);
  }
  return result;
}

static grpc_mdelem *remove_consumed_md(void *user_data, grpc_mdelem *md) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  size_t i;
  for (i = 0; i < calld->num_consumed_md; i++) {
    /* Maybe we could do a pointer comparison but we do not have any guarantee
       that the metadata processor used the same pointers for consumed_md in the
       callback. */
    if (memcmp(GPR_SLICE_START_PTR(md->key->slice), calld->consumed_md[i].key,
               GPR_SLICE_LENGTH(md->key->slice)) == 0 &&
        memcmp(GPR_SLICE_START_PTR(md->value->slice),
               calld->consumed_md[i].value,
               GPR_SLICE_LENGTH(md->value->slice)) == 0) {
      return NULL; /* Delete. */
    }
  }
  return md;
}

static void on_md_processing_done(void *user_data,
                                  const grpc_metadata *consumed_md,
                                  size_t num_consumed_md, int success,
                                  grpc_auth_context *result) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;

  calld->success = success;
  if (success) {
    calld->consumed_md = consumed_md;
    calld->num_consumed_md = num_consumed_md;
    grpc_metadata_batch_filter(&calld->md_op->data.metadata, remove_consumed_md,
                               elem);
    GPR_ASSERT(calld->call_auth_context != NULL);
    GRPC_AUTH_CONTEXT_UNREF(*calld->call_auth_context,
                            "releasing old context.");
    *calld->call_auth_context =
        GRPC_AUTH_CONTEXT_REF(result, "refing new context.");
  } else {
    grpc_call_element_send_cancel(elem);
  }

  calld->on_done_recv->cb(calld->on_done_recv->cb_arg, success);
}

static void auth_on_recv(void *user_data, int success) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (success) {
    size_t i;
    size_t nops = calld->recv_ops->nops;
    grpc_stream_op *ops = calld->recv_ops->ops;
    for (i = 0; i < nops; i++) {
      grpc_metadata_array md_array;
      grpc_process_auth_metadata_func processor =
          grpc_server_auth_context_get_process_metadata_func();
      grpc_stream_op *op = &ops[i];
      if (op->type != GRPC_OP_METADATA) continue;
      calld->got_client_metadata = 1;
      if (processor == NULL) continue;
      calld->md_op = op;
      md_array = metadata_batch_to_md_array(&op->data.metadata);
      processor(&calld->ticket, chand->security_connector->auth_context,
                md_array.metadata, md_array.count, on_md_processing_done, elem);
      grpc_metadata_array_destroy(&md_array);
      return;
    }
  }
  calld->on_done_recv->cb(calld->on_done_recv->cb_arg, success);
}

static void set_recv_ops_md_callbacks(grpc_call_element *elem,
                                      grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;

  if (op->send_ops && !calld->sent_status && !calld->success) {
    size_t i;
    size_t nops = op->send_ops->nops;
    grpc_stream_op *ops = op->send_ops->ops;
    for (i = 0; i < nops; i++) {
      grpc_stream_op *op = &ops[i];
      if (op->type != GRPC_OP_METADATA) continue;
      calld->sent_status = 1;
      grpc_metadata_batch_add_head(
          &op->data.metadata, &calld->status,
          GRPC_MDELEM_REF(chand->status_auth_failure));
      break;
    }
  }

  if (op->recv_ops && !calld->got_client_metadata) {
    /* substitute our callback for the higher callback */
    calld->recv_ops = op->recv_ops;
    calld->on_done_recv = op->on_done_recv;
    op->on_done_recv = &calld->auth_on_recv;
  }
}

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void auth_start_transport_op(grpc_call_element *elem,
                                    grpc_transport_stream_op *op) {
  set_recv_ops_md_callbacks(elem, op);
  grpc_call_next_op(elem, op);
}

/* Constructor for call_data */
static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_stream_op *initial_op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_server_security_context *server_ctx = NULL;

  /* initialize members */
  memset(calld, 0, sizeof(*calld));
  grpc_iomgr_closure_init(&calld->auth_on_recv, auth_on_recv, elem);
  calld->success = 1;

  GPR_ASSERT(initial_op && initial_op->context != NULL &&
             initial_op->context[GRPC_CONTEXT_SECURITY].value == NULL);

  /* Get the pollset for the ticket. */
  if (initial_op->bind_pollset) {
    calld->ticket.pollset = initial_op->bind_pollset;
  }

  /* Create a security context for the call and reference the auth context from
     the channel. */
  if (initial_op->context[GRPC_CONTEXT_SECURITY].value != NULL) {
    initial_op->context[GRPC_CONTEXT_SECURITY].destroy(
        initial_op->context[GRPC_CONTEXT_SECURITY].value);
  }
  server_ctx = grpc_server_security_context_create();
  server_ctx->auth_context = GRPC_AUTH_CONTEXT_REF(
      chand->security_connector->auth_context, "server_security_context");
  initial_op->context[GRPC_CONTEXT_SECURITY].value = server_ctx;
  initial_op->context[GRPC_CONTEXT_SECURITY].destroy =
      grpc_server_security_context_destroy;
  calld->call_auth_context = &server_ctx->auth_context;

  /* Set the metadata callbacks. */
  set_recv_ops_md_callbacks(elem, initial_op);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem, grpc_channel *master,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  grpc_security_connector *sc = grpc_find_security_connector_in_args(args);
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;

  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!is_first);
  GPR_ASSERT(!is_last);
  GPR_ASSERT(sc != NULL);

  /* initialize members */
  GPR_ASSERT(!sc->is_client_side);
  chand->security_connector =
      GRPC_SECURITY_CONNECTOR_REF(sc, "server_auth_filter");
  chand->status_auth_failure =
      grpc_mdelem_from_strings(mdctx, ":status", "401");
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;
  GRPC_SECURITY_CONNECTOR_UNREF(chand->security_connector,
                                "server_auth_filter");
  GRPC_MDELEM_UNREF(chand->status_auth_failure);
}

const grpc_channel_filter grpc_server_auth_filter = {
    auth_start_transport_op, grpc_channel_next_op, sizeof(call_data),
    init_call_elem,          destroy_call_elem,    sizeof(channel_data),
    init_channel_elem,       destroy_channel_elem, "server-auth"};
