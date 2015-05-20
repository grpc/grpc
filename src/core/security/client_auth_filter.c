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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/support/string.h"
#include "src/core/channel/channel_stack.h"
#include "src/core/security/security_context.h"
#include "src/core/security/security_connector.h"
#include "src/core/security/credentials.h"
#include "src/core/surface/call.h"

#define MAX_CREDENTIALS_METADATA_COUNT 4

/* We can have a per-call credentials. */
typedef struct {
  grpc_credentials *creds;
  grpc_mdstr *host;
  grpc_mdstr *method;
  grpc_transport_op op;
  size_t op_md_idx;
  int sent_initial_metadata;
  grpc_linked_mdelem md_links[MAX_CREDENTIALS_METADATA_COUNT];
} call_data;

/* We can have a per-channel credentials. */
typedef struct {
  grpc_channel_security_connector *security_connector;
  grpc_mdctx *md_ctx;
  grpc_mdstr *authority_string;
  grpc_mdstr *path_string;
  grpc_mdstr *error_msg_key;
  grpc_mdstr *status_key;
} channel_data;

static void bubble_up_error(grpc_call_element *elem, const char *error_msg) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_transport_op_add_cancellation(
      &calld->op, GRPC_STATUS_UNAUTHENTICATED,
      grpc_mdstr_from_string(chand->md_ctx, error_msg));
  grpc_call_next_op(elem, &calld->op);
}

static void on_credentials_metadata(void *user_data, grpc_mdelem **md_elems,
                                    size_t num_md,
                                    grpc_credentials_status status) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  call_data *calld = elem->call_data;
  grpc_transport_op *op = &calld->op;
  grpc_metadata_batch *mdb;
  size_t i;
  if (status != GRPC_CREDENTIALS_OK) {
    bubble_up_error(elem, "Credentials failed to get metadata.");
    return;
  }
  GPR_ASSERT(num_md <= MAX_CREDENTIALS_METADATA_COUNT);
  GPR_ASSERT(op->send_ops && op->send_ops->nops > calld->op_md_idx &&
             op->send_ops->ops[calld->op_md_idx].type == GRPC_OP_METADATA);
  mdb = &op->send_ops->ops[calld->op_md_idx].data.metadata;
  for (i = 0; i < num_md; i++) {
    grpc_metadata_batch_add_tail(mdb, &calld->md_links[i],
                                 grpc_mdelem_ref(md_elems[i]));
  }
  grpc_call_next_op(elem, op);
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

static void send_security_metadata(grpc_call_element *elem,
                                   grpc_transport_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_client_security_context *ctx =
      (grpc_client_security_context *)op->contexts[GRPC_CONTEXT_SECURITY].value;
  char *service_url = NULL;
  grpc_credentials *channel_creds =
      chand->security_connector->request_metadata_creds;
  int channel_creds_has_md =
      (channel_creds != NULL) &&
      grpc_credentials_has_request_metadata(channel_creds);
  int call_creds_has_md = (ctx != NULL) && (ctx->creds != NULL) &&
                          grpc_credentials_has_request_metadata(ctx->creds);

  if (!channel_creds_has_md && !call_creds_has_md) {
    /* Skip sending metadata altogether. */
    grpc_call_next_op(elem, op);
    return;
  }

  if (channel_creds_has_md && call_creds_has_md) {
    calld->creds = grpc_composite_credentials_create(channel_creds, ctx->creds);
    if (calld->creds == NULL) {
      bubble_up_error(elem,
                      "Incompatible credentials set on channel and call.");
      return;
    }
  } else {
    calld->creds =
        grpc_credentials_ref(call_creds_has_md ? ctx->creds : channel_creds);
  }

  service_url =
      build_service_url(chand->security_connector->base.url_scheme, calld);
  calld->op = *op; /* Copy op (originates from the caller's stack). */
  grpc_credentials_get_request_metadata(calld->creds, service_url,
                                        on_credentials_metadata, elem);
  gpr_free(service_url);
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
    bubble_up_error(elem, error_msg);
    gpr_free(error_msg);
  }
}

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void auth_start_transport_op(grpc_call_element *elem,
                                    grpc_transport_op *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_linked_mdelem *l;
  size_t i;

  if (op->send_ops && !calld->sent_initial_metadata) {
    size_t nops = op->send_ops->nops;
    grpc_stream_op *ops = op->send_ops->ops;
    for (i = 0; i < nops; i++) {
      grpc_stream_op *sop = &ops[i];
      if (sop->type != GRPC_OP_METADATA) continue;
      calld->op_md_idx = i;
      calld->sent_initial_metadata = 1;
      for (l = sop->data.metadata.list.head; l != NULL; l = l->next) {
        grpc_mdelem *md = l->md;
        /* Pointer comparison is OK for md_elems created from the same context.
         */
        if (md->key == chand->authority_string) {
          if (calld->host != NULL) grpc_mdstr_unref(calld->host);
          calld->host = grpc_mdstr_ref(md->value);
        } else if (md->key == chand->path_string) {
          if (calld->method != NULL) grpc_mdstr_unref(calld->method);
          calld->method = grpc_mdstr_ref(md->value);
        }
      }
      if (calld->host != NULL) {
        grpc_security_status status;
        const char *call_host = grpc_mdstr_as_c_string(calld->host);
        calld->op = *op; /* Copy op (originates from the caller's stack). */
        status = grpc_channel_security_connector_check_call_host(
            chand->security_connector, call_host, on_host_checked, elem);
        if (status != GRPC_SECURITY_OK) {
          if (status == GRPC_SECURITY_ERROR) {
            char *error_msg;
            gpr_asprintf(&error_msg,
                         "Invalid host %s set in :authority metadata.",
                         call_host);
            bubble_up_error(elem, error_msg);
            gpr_free(error_msg);
          }
          return; /* early exit */
        }
      }
      send_security_metadata(elem, op);
      return; /* early exit */
    }
  }

  /* pass control up or down the stack */
  grpc_call_next_op(elem, op);
}

/* Called on special channel events, such as disconnection or new incoming
   calls on the server */
static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  grpc_channel_next_op(elem, op);
}

/* Constructor for call_data */
static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_op *initial_op) {
  call_data *calld = elem->call_data;
  calld->creds = NULL;
  calld->host = NULL;
  calld->method = NULL;
  calld->sent_initial_metadata = 0;

  GPR_ASSERT(!initial_op || !initial_op->send_ops);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  grpc_credentials_unref(calld->creds);
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
  GPR_ASSERT(sc->is_client_side);
  chand->security_connector =
      (grpc_channel_security_connector *)grpc_security_connector_ref(sc);
  chand->md_ctx = metadata_context;
  chand->authority_string =
      grpc_mdstr_from_string(chand->md_ctx, ":authority");
  chand->path_string = grpc_mdstr_from_string(chand->md_ctx, ":path");
  chand->error_msg_key =
      grpc_mdstr_from_string(chand->md_ctx, "grpc-message");
  chand->status_key =
      grpc_mdstr_from_string(chand->md_ctx, "grpc-status");
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;
  grpc_channel_security_connector *ctx = chand->security_connector;
  if (ctx != NULL) grpc_security_connector_unref(&ctx->base);
  if (chand->authority_string != NULL) {
    grpc_mdstr_unref(chand->authority_string);
  }
  if (chand->error_msg_key != NULL) {
    grpc_mdstr_unref(chand->error_msg_key);
  }
  if (chand->status_key != NULL) {
    grpc_mdstr_unref(chand->status_key);
  }
  if (chand->path_string != NULL) {
    grpc_mdstr_unref(chand->path_string);
  }
}

const grpc_channel_filter grpc_client_auth_filter = {
    auth_start_transport_op, channel_op, sizeof(call_data), init_call_elem,
    destroy_call_elem, sizeof(channel_data), init_channel_elem,
    destroy_channel_elem, "client-auth"};
