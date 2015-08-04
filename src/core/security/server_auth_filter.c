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

#include "src/core/security/auth_filters.h"
#include "src/core/security/security_connector.h"
#include "src/core/security/security_context.h"

#include <grpc/support/log.h>

typedef struct call_data {
  int unused; /* C89 requires at least one struct element */
} call_data;

typedef struct channel_data {
  grpc_security_connector *security_connector;
} channel_data;

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void auth_start_transport_op(grpc_call_element *elem,
                                    grpc_transport_stream_op *op) {
  /* TODO(jboeuf): Get the metadata and get a new context from it. */

  /* pass control down the stack */
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
  calld->unused = 0;

  GPR_ASSERT(initial_op && initial_op->context != NULL &&
             initial_op->context[GRPC_CONTEXT_SECURITY].value == NULL);

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
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {}

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
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;
  GRPC_SECURITY_CONNECTOR_UNREF(chand->security_connector,
                                "server_auth_filter");
}

const grpc_channel_filter grpc_server_auth_filter = {
    auth_start_transport_op, grpc_channel_next_op,
    sizeof(call_data),       init_call_elem,
    destroy_call_elem,       sizeof(channel_data),
    init_channel_elem,       destroy_channel_elem,
    grpc_call_next_get_peer, "server-auth"};
