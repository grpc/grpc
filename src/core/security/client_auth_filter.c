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
#include <grpc/support/string_util.h>

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
  /* pollset bound to this call; if we need to make external
     network requests, they should be done under this pollset
     so that work can progress when this call wants work to
     progress */
  grpc_pollset *pollset;
  grpc_transport_stream_op op;
  size_t op_md_idx;
  int sent_initial_metadata;
  gpr_uint8 security_context_set;
  grpc_linked_mdelem md_links[MAX_CREDENTIALS_METADATA_COUNT];
  char *service_url;
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

static void reset_service_url(call_data *calld) {
  if (calld->service_url != NULL) {
    gpr_free(calld->service_url);
    calld->service_url = NULL;
  }
}

static void bubble_up_error(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                            grpc_status_code status, const char *error_msg) {
  call_data *calld = elem->call_data;
  gpr_log(GPR_ERROR, "Client side authentication failure: %s", error_msg);
  grpc_transport_stream_op_add_cancellation(&calld->op, status);
  grpc_call_next_op(exec_ctx, elem, &calld->op);
}

static void on_credentials_metadata(grpc_exec_ctx *exec_ctx, void *user_data,
                                    grpc_credentials_md *md_elems,
                                    size_t num_md,
                                    grpc_credentials_status status) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_transport_stream_op *op = &calld->op;
  grpc_metadata_batch *mdb;
  size_t i;
  reset_service_url(calld);
  if (status != GRPC_CREDENTIALS_OK) {
    bubble_up_error(exec_ctx, elem, GRPC_STATUS_UNAUTHENTICATED,
                    "Credentials failed to get metadata.");
    return;
  }
  GPR_ASSERT(num_md <= MAX_CREDENTIALS_METADATA_COUNT);
  GPR_ASSERT(op->send_ops && op->send_ops->nops > calld->op_md_idx &&
             op->send_ops->ops[calld->op_md_idx].type == GRPC_OP_METADATA);
  mdb = &op->send_ops->ops[calld->op_md_idx].data.metadata;
  for (i = 0; i < num_md; i++) {
    grpc_metadata_batch_add_tail(
        mdb, &calld->md_links[i],
        grpc_mdelem_from_slices(chand->md_ctx, gpr_slice_ref(md_elems[i].key),
                                gpr_slice_ref(md_elems[i].value)));
  }
  grpc_call_next_op(exec_ctx, elem, op);
}

void build_service_url(const char *url_scheme, call_data *calld) {
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
  reset_service_url(calld);
  gpr_asprintf(&calld->service_url, "%s://%s%s", url_scheme,
               grpc_mdstr_as_c_string(calld->host), service);
  gpr_free(service);
}

static void send_security_metadata(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem,
                                   grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_client_security_context *ctx =
      (grpc_client_security_context *)op->context[GRPC_CONTEXT_SECURITY].value;
  grpc_credentials *channel_creds =
      chand->security_connector->request_metadata_creds;
  int channel_creds_has_md =
      (channel_creds != NULL) &&
      grpc_credentials_has_request_metadata(channel_creds);
  int call_creds_has_md = (ctx != NULL) && (ctx->creds != NULL) &&
                          grpc_credentials_has_request_metadata(ctx->creds);

  if (!channel_creds_has_md && !call_creds_has_md) {
    /* Skip sending metadata altogether. */
    grpc_call_next_op(exec_ctx, elem, op);
    return;
  }

  if (channel_creds_has_md && call_creds_has_md) {
    calld->creds =
        grpc_composite_credentials_create(channel_creds, ctx->creds, NULL);
    if (calld->creds == NULL) {
      bubble_up_error(exec_ctx, elem, GRPC_STATUS_INVALID_ARGUMENT,
                      "Incompatible credentials set on channel and call.");
      return;
    }
  } else {
    calld->creds =
        grpc_credentials_ref(call_creds_has_md ? ctx->creds : channel_creds);
  }

  build_service_url(chand->security_connector->base.url_scheme, calld);
  calld->op = *op; /* Copy op (originates from the caller's stack). */
  GPR_ASSERT(calld->pollset);
  grpc_credentials_get_request_metadata(exec_ctx, calld->creds, calld->pollset,
                                        calld->service_url,
                                        on_credentials_metadata, elem);
}

static void on_host_checked(grpc_exec_ctx *exec_ctx, void *user_data,
                            grpc_security_status status) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  call_data *calld = elem->call_data;

  if (status == GRPC_SECURITY_OK) {
    send_security_metadata(exec_ctx, elem, &calld->op);
  } else {
    char *error_msg;
    gpr_asprintf(&error_msg, "Invalid host %s set in :authority metadata.",
                 grpc_mdstr_as_c_string(calld->host));
    bubble_up_error(exec_ctx, elem, GRPC_STATUS_INVALID_ARGUMENT, error_msg);
    gpr_free(error_msg);
  }
}

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void auth_start_transport_op(grpc_exec_ctx *exec_ctx,
                                    grpc_call_element *elem,
                                    grpc_transport_stream_op *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_linked_mdelem *l;
  size_t i;
  grpc_client_security_context *sec_ctx = NULL;

  if (calld->security_context_set == 0 &&
      op->cancel_with_status == GRPC_STATUS_OK) {
    calld->security_context_set = 1;
    GPR_ASSERT(op->context);
    if (op->context[GRPC_CONTEXT_SECURITY].value == NULL) {
      op->context[GRPC_CONTEXT_SECURITY].value =
          grpc_client_security_context_create();
      op->context[GRPC_CONTEXT_SECURITY].destroy =
          grpc_client_security_context_destroy;
    }
    sec_ctx = op->context[GRPC_CONTEXT_SECURITY].value;
    GRPC_AUTH_CONTEXT_UNREF(sec_ctx->auth_context, "client auth filter");
    sec_ctx->auth_context = GRPC_AUTH_CONTEXT_REF(
        chand->security_connector->base.auth_context, "client_auth_filter");
  }

  if (op->bind_pollset != NULL) {
    calld->pollset = op->bind_pollset;
  }

  if (op->send_ops != NULL && !calld->sent_initial_metadata) {
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
          if (calld->host != NULL) GRPC_MDSTR_UNREF(calld->host);
          calld->host = GRPC_MDSTR_REF(md->value);
        } else if (md->key == chand->path_string) {
          if (calld->method != NULL) GRPC_MDSTR_UNREF(calld->method);
          calld->method = GRPC_MDSTR_REF(md->value);
        }
      }
      if (calld->host != NULL) {
        grpc_security_status status;
        const char *call_host = grpc_mdstr_as_c_string(calld->host);
        calld->op = *op; /* Copy op (originates from the caller's stack). */
        status = grpc_channel_security_connector_check_call_host(
            exec_ctx, chand->security_connector, call_host, on_host_checked,
            elem);
        if (status != GRPC_SECURITY_OK) {
          if (status == GRPC_SECURITY_ERROR) {
            char *error_msg;
            gpr_asprintf(&error_msg,
                         "Invalid host %s set in :authority metadata.",
                         call_host);
            bubble_up_error(exec_ctx, elem, GRPC_STATUS_INVALID_ARGUMENT,
                            error_msg);
            gpr_free(error_msg);
          }
          return; /* early exit */
        }
      }
      send_security_metadata(exec_ctx, elem, op);
      return; /* early exit */
    }
  }

  /* pass control down the stack */
  grpc_call_next_op(exec_ctx, elem, op);
}

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_stream_op *initial_op) {
  call_data *calld = elem->call_data;
  memset(calld, 0, sizeof(*calld));
  GPR_ASSERT(!initial_op || !initial_op->send_ops);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx,
                              grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  grpc_credentials_unref(calld->creds);
  if (calld->host != NULL) {
    GRPC_MDSTR_UNREF(calld->host);
  }
  if (calld->method != NULL) {
    GRPC_MDSTR_UNREF(calld->method);
  }
  reset_service_url(calld);
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem, grpc_channel *master,
                              const grpc_channel_args *args,
                              grpc_mdctx *metadata_context, int is_first,
                              int is_last) {
  grpc_security_connector *sc = grpc_find_security_connector_in_args(args);
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;

  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!is_last);
  GPR_ASSERT(sc != NULL);

  /* initialize members */
  GPR_ASSERT(sc->is_client_side);
  chand->security_connector =
      (grpc_channel_security_connector *)GRPC_SECURITY_CONNECTOR_REF(
          sc, "client_auth_filter");
  chand->md_ctx = metadata_context;
  chand->authority_string = grpc_mdstr_from_string(chand->md_ctx, ":authority");
  chand->path_string = grpc_mdstr_from_string(chand->md_ctx, ":path");
  chand->error_msg_key = grpc_mdstr_from_string(chand->md_ctx, "grpc-message");
  chand->status_key = grpc_mdstr_from_string(chand->md_ctx, "grpc-status");
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;
  grpc_channel_security_connector *ctx = chand->security_connector;
  if (ctx != NULL)
    GRPC_SECURITY_CONNECTOR_UNREF(&ctx->base, "client_auth_filter");
  if (chand->authority_string != NULL) {
    GRPC_MDSTR_UNREF(chand->authority_string);
  }
  if (chand->error_msg_key != NULL) {
    GRPC_MDSTR_UNREF(chand->error_msg_key);
  }
  if (chand->status_key != NULL) {
    GRPC_MDSTR_UNREF(chand->status_key);
  }
  if (chand->path_string != NULL) {
    GRPC_MDSTR_UNREF(chand->path_string);
  }
}

const grpc_channel_filter grpc_client_auth_filter = {
    auth_start_transport_op, grpc_channel_next_op, sizeof(call_data),
    init_call_elem,          destroy_call_elem,    sizeof(channel_data),
    init_channel_elem,       destroy_channel_elem, grpc_call_next_get_peer,
    "client-auth"};
