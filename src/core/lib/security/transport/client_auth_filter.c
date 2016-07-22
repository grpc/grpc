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

#include "src/core/lib/security/transport/auth_filters.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/security_connector.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/static_metadata.h"

#define MAX_CREDENTIALS_METADATA_COUNT 4

/* We can have a per-call credentials. */
typedef struct {
  grpc_call_credentials *creds;
  grpc_mdstr *host;
  grpc_mdstr *method;
  /* pollset{_set} bound to this call; if we need to make external
     network requests, they should be done under a pollset added to this
     pollset_set so that work can progress when this call wants work to progress
  */
  grpc_polling_entity *pollent;
  grpc_transport_stream_op op;
  uint8_t security_context_set;
  grpc_linked_mdelem md_links[MAX_CREDENTIALS_METADATA_COUNT];
  grpc_auth_metadata_context auth_md_context;
} call_data;

/* We can have a per-channel credentials. */
typedef struct {
  grpc_channel_security_connector *security_connector;
  grpc_auth_context *auth_context;
} channel_data;

static void reset_auth_metadata_context(
    grpc_auth_metadata_context *auth_md_context) {
  if (auth_md_context->service_url != NULL) {
    gpr_free((char *)auth_md_context->service_url);
    auth_md_context->service_url = NULL;
  }
  if (auth_md_context->method_name != NULL) {
    gpr_free((char *)auth_md_context->method_name);
    auth_md_context->method_name = NULL;
  }
  GRPC_AUTH_CONTEXT_UNREF(
      (grpc_auth_context *)auth_md_context->channel_auth_context,
      "grpc_auth_metadata_context");
  auth_md_context->channel_auth_context = NULL;
}

static void bubble_up_error(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                            grpc_status_code status, const char *error_msg) {
  call_data *calld = elem->call_data;
  gpr_log(GPR_ERROR, "Client side authentication failure: %s", error_msg);
  gpr_slice error_slice = gpr_slice_from_copied_string(error_msg);
  grpc_transport_stream_op_add_close(&calld->op, status, &error_slice);
  grpc_call_next_op(exec_ctx, elem, &calld->op);
}

static void on_credentials_metadata(grpc_exec_ctx *exec_ctx, void *user_data,
                                    grpc_credentials_md *md_elems,
                                    size_t num_md,
                                    grpc_credentials_status status,
                                    const char *error_details) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  call_data *calld = elem->call_data;
  grpc_transport_stream_op *op = &calld->op;
  grpc_metadata_batch *mdb;
  size_t i;
  reset_auth_metadata_context(&calld->auth_md_context);
  if (status != GRPC_CREDENTIALS_OK) {
    bubble_up_error(exec_ctx, elem, GRPC_STATUS_UNAUTHENTICATED,
                    (error_details != NULL && strlen(error_details) > 0)
                        ? error_details
                        : "Credentials failed to get metadata.");
    return;
  }
  GPR_ASSERT(num_md <= MAX_CREDENTIALS_METADATA_COUNT);
  GPR_ASSERT(op->send_initial_metadata != NULL);
  mdb = op->send_initial_metadata;
  for (i = 0; i < num_md; i++) {
    grpc_metadata_batch_add_tail(
        mdb, &calld->md_links[i],
        grpc_mdelem_from_slices(gpr_slice_ref(md_elems[i].key),
                                gpr_slice_ref(md_elems[i].value)));
  }
  grpc_call_next_op(exec_ctx, elem, op);
}

void build_auth_metadata_context(grpc_security_connector *sc,
                                 grpc_auth_context *auth_context,
                                 call_data *calld) {
  char *service = gpr_strdup(grpc_mdstr_as_c_string(calld->method));
  char *last_slash = strrchr(service, '/');
  char *method_name = NULL;
  char *service_url = NULL;
  reset_auth_metadata_context(&calld->auth_md_context);
  if (last_slash == NULL) {
    gpr_log(GPR_ERROR, "No '/' found in fully qualified method name");
    service[0] = '\0';
  } else if (last_slash == service) {
    /* No service part in fully qualified method name: will just be "/". */
    service[1] = '\0';
  } else {
    *last_slash = '\0';
    method_name = gpr_strdup(last_slash + 1);
  }
  if (method_name == NULL) method_name = gpr_strdup("");
  gpr_asprintf(&service_url, "%s://%s%s",
               sc->url_scheme == NULL ? "" : sc->url_scheme,
               grpc_mdstr_as_c_string(calld->host), service);
  calld->auth_md_context.service_url = service_url;
  calld->auth_md_context.method_name = method_name;
  calld->auth_md_context.channel_auth_context =
      GRPC_AUTH_CONTEXT_REF(auth_context, "grpc_auth_metadata_context");
  gpr_free(service);
}

static void send_security_metadata(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem,
                                   grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_client_security_context *ctx =
      (grpc_client_security_context *)op->context[GRPC_CONTEXT_SECURITY].value;
  grpc_call_credentials *channel_call_creds =
      chand->security_connector->request_metadata_creds;
  int call_creds_has_md = (ctx != NULL) && (ctx->creds != NULL);

  if (channel_call_creds == NULL && !call_creds_has_md) {
    /* Skip sending metadata altogether. */
    grpc_call_next_op(exec_ctx, elem, op);
    return;
  }

  if (channel_call_creds != NULL && call_creds_has_md) {
    calld->creds = grpc_composite_call_credentials_create(channel_call_creds,
                                                          ctx->creds, NULL);
    if (calld->creds == NULL) {
      bubble_up_error(exec_ctx, elem, GRPC_STATUS_UNAUTHENTICATED,
                      "Incompatible credentials set on channel and call.");
      return;
    }
  } else {
    calld->creds = grpc_call_credentials_ref(
        call_creds_has_md ? ctx->creds : channel_call_creds);
  }

  build_auth_metadata_context(&chand->security_connector->base,
                              chand->auth_context, calld);
  calld->op = *op; /* Copy op (originates from the caller's stack). */
  GPR_ASSERT(calld->pollent != NULL);
  grpc_call_credentials_get_request_metadata(
      exec_ctx, calld->creds, calld->pollent, calld->auth_md_context,
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
    bubble_up_error(exec_ctx, elem, GRPC_STATUS_UNAUTHENTICATED, error_msg);
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
  grpc_client_security_context *sec_ctx = NULL;

  if (calld->security_context_set == 0 && op->cancel_error == GRPC_ERROR_NONE) {
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
    sec_ctx->auth_context =
        GRPC_AUTH_CONTEXT_REF(chand->auth_context, "client_auth_filter");
  }

  if (op->send_initial_metadata != NULL) {
    for (l = op->send_initial_metadata->list.head; l != NULL; l = l->next) {
      grpc_mdelem *md = l->md;
      /* Pointer comparison is OK for md_elems created from the same context.
       */
      if (md->key == GRPC_MDSTR_AUTHORITY) {
        if (calld->host != NULL) GRPC_MDSTR_UNREF(calld->host);
        calld->host = GRPC_MDSTR_REF(md->value);
      } else if (md->key == GRPC_MDSTR_PATH) {
        if (calld->method != NULL) GRPC_MDSTR_UNREF(calld->method);
        calld->method = GRPC_MDSTR_REF(md->value);
      }
    }
    if (calld->host != NULL) {
      const char *call_host = grpc_mdstr_as_c_string(calld->host);
      calld->op = *op; /* Copy op (originates from the caller's stack). */
      grpc_channel_security_connector_check_call_host(
          exec_ctx, chand->security_connector, call_host, chand->auth_context,
          on_host_checked, elem);
      return; /* early exit */
    }
  }

  /* pass control down the stack */
  grpc_call_next_op(exec_ctx, elem, op);
}

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  memset(calld, 0, sizeof(*calld));
}

static void set_pollset_or_pollset_set(grpc_exec_ctx *exec_ctx,
                                       grpc_call_element *elem,
                                       grpc_polling_entity *pollent) {
  call_data *calld = elem->call_data;
  calld->pollent = pollent;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_stats *stats, void *ignored) {
  call_data *calld = elem->call_data;
  grpc_call_credentials_unref(calld->creds);
  if (calld->host != NULL) {
    GRPC_MDSTR_UNREF(calld->host);
  }
  if (calld->method != NULL) {
    GRPC_MDSTR_UNREF(calld->method);
  }
  reset_auth_metadata_context(&calld->auth_md_context);
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem,
                              grpc_channel_element_args *args) {
  grpc_security_connector *sc =
      grpc_find_security_connector_in_args(args->channel_args);
  grpc_auth_context *auth_context =
      grpc_find_auth_context_in_args(args->channel_args);

  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;

  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!args->is_last);
  GPR_ASSERT(sc != NULL);
  GPR_ASSERT(auth_context != NULL);

  /* initialize members */
  chand->security_connector =
      (grpc_channel_security_connector *)GRPC_SECURITY_CONNECTOR_REF(
          sc, "client_auth_filter");
  chand->auth_context =
      GRPC_AUTH_CONTEXT_REF(auth_context, "client_auth_filter");
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;
  grpc_channel_security_connector *sc = chand->security_connector;
  if (sc != NULL) {
    GRPC_SECURITY_CONNECTOR_UNREF(&sc->base, "client_auth_filter");
  }
  GRPC_AUTH_CONTEXT_UNREF(chand->auth_context, "client_auth_filter");
}

const grpc_channel_filter grpc_client_auth_filter = {auth_start_transport_op,
                                                     grpc_channel_next_op,
                                                     sizeof(call_data),
                                                     init_call_elem,
                                                     set_pollset_or_pollset_set,
                                                     destroy_call_elem,
                                                     sizeof(channel_data),
                                                     init_channel_elem,
                                                     destroy_channel_elem,
                                                     grpc_call_next_get_peer,
                                                     "client-auth"};
