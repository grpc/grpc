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

#include "src/core/security/auth.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/support/string.h"
#include "src/core/channel/channel_stack.h"
#include "src/core/security/security_context.h"
#include "src/core/security/credentials.h"
#include "src/core/surface/call.h"

/* We can have a per-call credentials. */
typedef struct {
  grpc_credentials *creds;
  grpc_mdstr *host;
  grpc_mdstr *method;
  grpc_call_op op;
} call_data;

/* We can have a per-channel credentials. */
typedef struct {
  grpc_channel_security_context *security_context;
  grpc_mdctx *md_ctx;
  grpc_mdstr *authority_string;
  grpc_mdstr *path_string;
  grpc_mdstr *error_msg_key;
  grpc_mdstr *status_key;
} channel_data;

static void do_nothing(void *ignored, grpc_op_error error) {}

static void bubbleup_error(grpc_call_element *elem, const char *error_msg) {
  grpc_call_op finish_op;
  channel_data *channeld = elem->channel_data;
  char status[GPR_LTOA_MIN_BUFSIZE];

  gpr_log(GPR_ERROR, "%s", error_msg);
  finish_op.type = GRPC_RECV_METADATA;
  finish_op.dir = GRPC_CALL_UP;
  finish_op.flags = 0;
  finish_op.data.metadata = grpc_mdelem_from_metadata_strings(
      channeld->md_ctx, grpc_mdstr_ref(channeld->error_msg_key),
      grpc_mdstr_from_string(channeld->md_ctx, error_msg));
  finish_op.done_cb = do_nothing;
  finish_op.user_data = NULL;
  grpc_call_next_op(elem, &finish_op);

  gpr_ltoa(GRPC_STATUS_UNAUTHENTICATED, status);
  finish_op.data.metadata = grpc_mdelem_from_metadata_strings(
      channeld->md_ctx, grpc_mdstr_ref(channeld->status_key),
      grpc_mdstr_from_string(channeld->md_ctx, status));
  grpc_call_next_op(elem, &finish_op);

  grpc_call_element_send_cancel(elem);
}

static void on_credentials_metadata(void *user_data, grpc_mdelem **md_elems,
                                    size_t num_md,
                                    grpc_credentials_status status) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  size_t i;
  for (i = 0; i < num_md; i++) {
    grpc_call_element_send_metadata(elem, grpc_mdelem_ref(md_elems[i]));
  }
  grpc_call_next_op(elem, &((call_data *)elem->call_data)->op);
}

static char *build_service_url(const char *url_scheme, call_data *calld) {
  char *service_url;
  char *service = gpr_strdup(grpc_mdstr_as_c_string(calld->method));
  char *last_slash = strrchr(service, '/');
  if (last_slash == NULL) {
    gpr_log(GPR_ERROR, "No '/' found in fully qualified method name");
    service[0] = '\0';
  } else if (last_slash == service) {
    /* No service part in fully qualified method name: will just be "/". */
    service[1] = '\0';
  } else {
    *last_slash = '\0';
  }
  if (url_scheme == NULL) url_scheme = "";
  gpr_asprintf(&service_url, "%s://%s%s", url_scheme,
               grpc_mdstr_as_c_string(calld->host), service);
  gpr_free(service);
  return service_url;
}

static void send_security_metadata(grpc_call_element *elem, grpc_call_op *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;

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
    char *service_url =
        build_service_url(channeld->security_context->base.url_scheme, calld);
    calld->op = *op; /* Copy op (originates from the caller's stack). */
    grpc_credentials_get_request_metadata(channel_creds, service_url,
                                          on_credentials_metadata, elem);
    gpr_free(service_url);
  } else {
    grpc_call_next_op(elem, op);
  }
}

static void on_host_checked(void *user_data, grpc_security_status status) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  call_data *calld = elem->call_data;

  if (status == GRPC_SECURITY_OK) {
    send_security_metadata(elem, &calld->op);
  } else {
    char *error_msg;
    gpr_asprintf(&error_msg, "Invalid host %s set in :authority metadata.",
                 grpc_mdstr_as_c_string(calld->host));
    bubbleup_error(elem, error_msg);
    gpr_free(error_msg);
    calld->op.done_cb(calld->op.user_data, GRPC_OP_ERROR);
  }
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
    case GRPC_SEND_METADATA:
      /* Pointer comparison is OK for md_elems created from the same context. */
      if (op->data.metadata->key == channeld->authority_string) {
        if (calld->host != NULL) grpc_mdstr_unref(calld->host);
        calld->host = grpc_mdstr_ref(op->data.metadata->value);
      } else if (op->data.metadata->key == channeld->path_string) {
        if (calld->method != NULL) grpc_mdstr_unref(calld->method);
        calld->method = grpc_mdstr_ref(op->data.metadata->value);
      }
      grpc_call_next_op(elem, op);
      break;

    case GRPC_SEND_START:
      if (calld->host != NULL) {
        grpc_security_status status;
        const char *call_host = grpc_mdstr_as_c_string(calld->host);
        calld->op = *op; /* Copy op (originates from the caller's stack). */
        status = grpc_channel_security_context_check_call_host(
            channeld->security_context, call_host, on_host_checked, elem);
        if (status != GRPC_SECURITY_OK) {
          if (status == GRPC_SECURITY_ERROR) {
            char *error_msg;
            gpr_asprintf(&error_msg,
                         "Invalid host %s set in :authority metadata.",
                         call_host);
            bubbleup_error(elem, error_msg);
            gpr_free(error_msg);
            op->done_cb(op->user_data, GRPC_OP_ERROR);
          }
          break;
        }
      }
      send_security_metadata(elem, op);
      break;

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
  calld->host = NULL;
  calld->method = NULL;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  if (calld->creds != NULL) {
    grpc_credentials_unref(calld->creds);
  }
  if (calld->host != NULL) {
    grpc_mdstr_unref(calld->host);
  }
  if (calld->method != NULL) {
    grpc_mdstr_unref(calld->method);
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
  channeld->md_ctx = metadata_context;
  channeld->authority_string =
      grpc_mdstr_from_string(channeld->md_ctx, ":authority");
  channeld->path_string = grpc_mdstr_from_string(channeld->md_ctx, ":path");
  channeld->error_msg_key =
      grpc_mdstr_from_string(channeld->md_ctx, "grpc-message");
  channeld->status_key = grpc_mdstr_from_string(channeld->md_ctx, "grpc-status");
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *channeld = elem->channel_data;
  grpc_channel_security_context *ctx = channeld->security_context;
  if (ctx != NULL) grpc_security_context_unref(&ctx->base);
  if (channeld->authority_string != NULL) {
    grpc_mdstr_unref(channeld->authority_string);
  }
  if (channeld->error_msg_key != NULL) {
    grpc_mdstr_unref(channeld->error_msg_key);
  }
  if (channeld->status_key != NULL) {
    grpc_mdstr_unref(channeld->status_key);
  }
  if (channeld->path_string != NULL) {
    grpc_mdstr_unref(channeld->path_string);
  }
}

const grpc_channel_filter grpc_client_auth_filter = {
    call_op,           channel_op,           sizeof(call_data),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "auth"};
