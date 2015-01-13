/*
 *
 * Copyright 2014, Google Inc.
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

#include "src/core/security/auth.h"

#include <string.h>

#include "src/core/security/security_context.h"
#include "src/core/security/credentials.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

/* We can have a per-call credentials. */
typedef struct {
  grpc_credentials *creds;
  grpc_call_op op;
} call_data;

/* We can have a per-channel credentials. */
typedef struct {
  grpc_channel_security_context *security_context;
} channel_data;

static void on_credentials_metadata(void *user_data, grpc_mdelem **md_elems,
                                    size_t num_md,
                                    grpc_credentials_status status) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  size_t i;
  for (i = 0; i < num_md; i++) {
    grpc_call_element_send_metadata(elem, md_elems[i]);
  }
  grpc_call_next_op(elem, &((call_data *)elem->call_data)->op);
}

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void call_op(grpc_call_element *elem, grpc_call_element *from_elem,
                    grpc_call_op *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;

  switch (op->type) {
    case GRPC_SEND_START: {
      grpc_credentials *channel_creds =
          channeld->security_context->request_metadata_creds;
      /* TODO(jboeuf):
         Decide on the policy in this case:
         - populate both channel and call?
         - the call takes precedence over the channel?
         - leave this decision up to the channel credentials?  */
      if (calld->creds != NULL) {
        gpr_log(GPR_ERROR, "Ignoring per call credentials for now.");
      }
      if (channel_creds != NULL &&
          grpc_credentials_has_request_metadata(channel_creds)) {
        calld->op = *op; /* Copy op (originates from the caller's stack). */
        grpc_credentials_get_request_metadata(channel_creds,
                                              on_credentials_metadata, elem);
        break;
      }
      /* FALLTHROUGH INTENDED. */
    }

    default:
      /* pass control up or down the stack depending on op->dir */
      grpc_call_next_op(elem, op);
      break;
  }
}

/* Called on special channel events, such as disconnection or new incoming
   calls on the server */
static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  grpc_channel_next_op(elem, op);
}

/* Constructor for call_data */
static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data) {
  /* TODO(jboeuf):
     Find a way to pass-in the credentials from the caller here.  */
  call_data *calld = elem->call_data;
  calld->creds = NULL;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  if (calld->creds != NULL) {
    grpc_credentials_unref(calld->creds);
  }
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args,
                              grpc_mdctx *metadata_context, int is_first,
                              int is_last) {
  grpc_security_context *ctx = grpc_find_security_context_in_args(args);
  /* grab pointers to our data from the channel element */
  channel_data *channeld = elem->channel_data;

  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!is_first);
  GPR_ASSERT(!is_last);
  GPR_ASSERT(ctx != NULL);

  /* initialize members */
  GPR_ASSERT(ctx->is_client_side);
  channeld->security_context =
      (grpc_channel_security_context *)grpc_security_context_ref(ctx);
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *channeld = elem->channel_data;
  grpc_channel_security_context *ctx = channeld->security_context;
  if (ctx != NULL) grpc_security_context_unref(&ctx->base);
}

const grpc_channel_filter grpc_client_auth_filter = {
    call_op, channel_op, sizeof(call_data), init_call_elem, destroy_call_elem,
    sizeof(channel_data), init_channel_elem, destroy_channel_elem, "auth"};
