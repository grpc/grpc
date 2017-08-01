/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/security/transport/auth_filters.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/security_connector.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/static_metadata.h"

#define MAX_CREDENTIALS_METADATA_COUNT 4

/* We can have a per-call credentials. */
typedef struct {
  grpc_call_credentials *creds;
  bool have_host;
  bool have_method;
  grpc_slice host;
  grpc_slice method;
  /* pollset{_set} bound to this call; if we need to make external
     network requests, they should be done under a pollset added to this
     pollset_set so that work can progress when this call wants work to progress
  */
  grpc_polling_entity *pollent;
  gpr_atm security_context_set;
  gpr_mu security_context_mu;
  grpc_credentials_mdelem_array md_array;
  grpc_linked_mdelem md_links[MAX_CREDENTIALS_METADATA_COUNT];
  grpc_auth_metadata_context auth_md_context;
  grpc_closure closure;
  // Either 0 (no cancellation and no async operation in flight),
  // a grpc_closure* (if the lowest bit is 0),
  // or a grpc_error* (if the lowest bit is 1).
  gpr_atm cancellation_state;
  grpc_closure cancel_closure;
} call_data;

/* We can have a per-channel credentials. */
typedef struct {
  grpc_channel_security_connector *security_connector;
  grpc_auth_context *auth_context;
} channel_data;

static void decode_cancel_state(gpr_atm cancel_state, grpc_closure **func,
                                grpc_error **error) {
  // If the lowest bit is 1, the value is a grpc_error*.
  // Otherwise, if non-zdero, the value is a grpc_closure*.
  if (cancel_state & 1) {
    *error = (grpc_error *)(cancel_state & ~(gpr_atm)1);
  } else if (cancel_state != 0) {
    *func = (grpc_closure *)cancel_state;
  }
}

static gpr_atm encode_cancel_state_error(grpc_error *error) {
  // Set the lowest bit to 1 to indicate that it's an error.
  return (gpr_atm)1 | (gpr_atm)error;
}

// Returns an error if the call has been cancelled.  Otherwise, sets the
// cancellation function to be called upon cancellation.
static grpc_error *set_cancel_func(grpc_call_element *elem,
                                   grpc_iomgr_cb_func func) {
  call_data *calld = (call_data *)elem->call_data;
  // Decode original state.
  gpr_atm original_state = gpr_atm_acq_load(&calld->cancellation_state);
  grpc_error *original_error = GRPC_ERROR_NONE;
  grpc_closure *original_func = NULL;
  decode_cancel_state(original_state, &original_func, &original_error);
  // If error is set, return it.
  if (original_error != GRPC_ERROR_NONE) return GRPC_ERROR_REF(original_error);
  // Otherwise, store func.
  GRPC_CLOSURE_INIT(&calld->cancel_closure, func, elem,
                    grpc_schedule_on_exec_ctx);
  GPR_ASSERT(((gpr_atm)&calld->cancel_closure & (gpr_atm)1) == 0);
  gpr_atm_rel_store(&calld->cancellation_state,
                    (gpr_atm)&calld->cancel_closure);
  return GRPC_ERROR_NONE;
}

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

static void add_error(grpc_error **combined, grpc_error *error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*combined == GRPC_ERROR_NONE) {
    *combined = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Client auth metadata plugin error");
  }
  *combined = grpc_error_add_child(*combined, error);
}

static void on_credentials_metadata(grpc_exec_ctx *exec_ctx, void *arg,
                                    grpc_error *input_error) {
  grpc_transport_stream_op_batch *batch = (grpc_transport_stream_op_batch *)arg;
  grpc_call_element *elem = batch->handler_private.extra_arg;
  call_data *calld = elem->call_data;
  reset_auth_metadata_context(&calld->auth_md_context);
  grpc_error *error = GRPC_ERROR_REF(input_error);
  if (error == GRPC_ERROR_NONE) {
    GPR_ASSERT(calld->md_array.size <= MAX_CREDENTIALS_METADATA_COUNT);
    GPR_ASSERT(batch->send_initial_metadata);
    grpc_metadata_batch *mdb =
        batch->payload->send_initial_metadata.send_initial_metadata;
    for (size_t i = 0; i < calld->md_array.size; ++i) {
      add_error(&error, grpc_metadata_batch_add_tail(
                            exec_ctx, mdb, &calld->md_links[i],
                            GRPC_MDELEM_REF(calld->md_array.md[i])));
    }
  }
  if (error == GRPC_ERROR_NONE) {
    grpc_call_next_op(exec_ctx, elem, batch);
  } else {
    error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                               GRPC_STATUS_UNAUTHENTICATED);
    grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, batch, error);
  }
}

void build_auth_metadata_context(grpc_security_connector *sc,
                                 grpc_auth_context *auth_context,
                                 call_data *calld) {
  char *service = grpc_slice_to_c_string(calld->method);
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
  char *host = grpc_slice_to_c_string(calld->host);
  gpr_asprintf(&service_url, "%s://%s%s",
               sc->url_scheme == NULL ? "" : sc->url_scheme, host, service);
  calld->auth_md_context.service_url = service_url;
  calld->auth_md_context.method_name = method_name;
  calld->auth_md_context.channel_auth_context =
      GRPC_AUTH_CONTEXT_REF(auth_context, "grpc_auth_metadata_context");
  gpr_free(service);
  gpr_free(host);
}

static void cancel_get_request_metadata(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
  grpc_call_element *elem = (grpc_call_element *)arg;
  call_data *calld = (call_data *)elem->call_data;
  grpc_call_credentials_cancel_get_request_metadata(
      exec_ctx, calld->creds, &calld->md_array, GRPC_ERROR_REF(error));
}

static void send_security_metadata(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem,
                                   grpc_transport_stream_op_batch *batch) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_client_security_context *ctx =
      (grpc_client_security_context *)batch->payload
          ->context[GRPC_CONTEXT_SECURITY]
          .value;
  grpc_call_credentials *channel_call_creds =
      chand->security_connector->request_metadata_creds;
  int call_creds_has_md = (ctx != NULL) && (ctx->creds != NULL);

  if (channel_call_creds == NULL && !call_creds_has_md) {
    /* Skip sending metadata altogether. */
    grpc_call_next_op(exec_ctx, elem, batch);
    return;
  }

  if (channel_call_creds != NULL && call_creds_has_md) {
    calld->creds = grpc_composite_call_credentials_create(channel_call_creds,
                                                          ctx->creds, NULL);
    if (calld->creds == NULL) {
      grpc_transport_stream_op_batch_finish_with_failure(
          exec_ctx, batch,
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "Incompatible credentials set on channel and call."),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAUTHENTICATED));
      return;
    }
  } else {
    calld->creds = grpc_call_credentials_ref(
        call_creds_has_md ? ctx->creds : channel_call_creds);
  }

  build_auth_metadata_context(&chand->security_connector->base,
                              chand->auth_context, calld);

  grpc_error *cancel_error = set_cancel_func(elem, cancel_get_request_metadata);
  if (cancel_error != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, batch,
                                                       cancel_error);
    return;
  }
  GPR_ASSERT(calld->pollent != NULL);
  GRPC_CLOSURE_INIT(&calld->closure, on_credentials_metadata, batch,
                    grpc_schedule_on_exec_ctx);
  grpc_error *error = GRPC_ERROR_NONE;
  if (grpc_call_credentials_get_request_metadata(
          exec_ctx, calld->creds, calld->pollent, calld->auth_md_context,
          &calld->md_array, &calld->closure, &error)) {
    // Synchronous return; invoke on_credentials_metadata() directly.
    on_credentials_metadata(exec_ctx, batch, error);
    GRPC_ERROR_UNREF(error);
  }
}

static void on_host_checked(grpc_exec_ctx *exec_ctx, void *arg,
                            grpc_error *error) {
  grpc_transport_stream_op_batch *batch = (grpc_transport_stream_op_batch *)arg;
  grpc_call_element *elem = batch->handler_private.extra_arg;
  call_data *calld = elem->call_data;

  if (error == GRPC_ERROR_NONE) {
    send_security_metadata(exec_ctx, elem, batch);
  } else {
    char *error_msg;
    char *host = grpc_slice_to_c_string(calld->host);
    gpr_asprintf(&error_msg, "Invalid host %s set in :authority metadata.",
                 host);
    gpr_free(host);
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, batch,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_UNAUTHENTICATED));
    gpr_free(error_msg);
  }
}

static void cancel_check_call_host(grpc_exec_ctx *exec_ctx, void *arg,
                                   grpc_error *error) {
  grpc_call_element *elem = (grpc_call_element *)arg;
  call_data *calld = (call_data *)elem->call_data;
  channel_data *chand = (channel_data *)elem->channel_data;
  grpc_channel_security_connector_cancel_check_call_host(
      exec_ctx, chand->security_connector, &calld->closure,
      GRPC_ERROR_REF(error));
}

static void auth_start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *batch) {
  GPR_TIMER_BEGIN("auth_start_transport_stream_op_batch", 0);

  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;

  if (batch->cancel_stream) {
    while (true) {
      // Decode the original cancellation state.
      gpr_atm original_state = gpr_atm_acq_load(&calld->cancellation_state);
      grpc_error *cancel_error = GRPC_ERROR_NONE;
      grpc_closure *func = NULL;
      decode_cancel_state(original_state, &func, &cancel_error);
      // If we had already set a cancellation error, there's nothing
      // more to do.
      if (cancel_error != GRPC_ERROR_NONE) break;
      // If there's a cancel func, call it.
      // Note that even if the cancel func has been changed by some
      // other thread between when we decoded it and now, it will just
      // be a no-op.
      cancel_error = GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
      if (func != NULL) {
        GRPC_CLOSURE_SCHED(exec_ctx, func, GRPC_ERROR_REF(cancel_error));
      }
      // Encode the new error into cancellation state.
      if (gpr_atm_full_cas(&calld->cancellation_state, original_state,
                           encode_cancel_state_error(cancel_error))) {
        break;  // Success.
      }
      // The cas failed, so try again.
    }
  } else {
    /* double checked lock over security context to ensure it's set once */
    if (gpr_atm_acq_load(&calld->security_context_set) == 0) {
      gpr_mu_lock(&calld->security_context_mu);
      if (gpr_atm_acq_load(&calld->security_context_set) == 0) {
        GPR_ASSERT(batch->payload->context != NULL);
        if (batch->payload->context[GRPC_CONTEXT_SECURITY].value == NULL) {
          batch->payload->context[GRPC_CONTEXT_SECURITY].value =
              grpc_client_security_context_create();
          batch->payload->context[GRPC_CONTEXT_SECURITY].destroy =
              grpc_client_security_context_destroy;
        }
        grpc_client_security_context *sec_ctx =
            batch->payload->context[GRPC_CONTEXT_SECURITY].value;
        GRPC_AUTH_CONTEXT_UNREF(sec_ctx->auth_context, "client auth filter");
        sec_ctx->auth_context =
            GRPC_AUTH_CONTEXT_REF(chand->auth_context, "client_auth_filter");
        gpr_atm_rel_store(&calld->security_context_set, 1);
      }
      gpr_mu_unlock(&calld->security_context_mu);
    }
  }

  if (batch->send_initial_metadata) {
    for (grpc_linked_mdelem *l = batch->payload->send_initial_metadata
                                     .send_initial_metadata->list.head;
         l != NULL; l = l->next) {
      grpc_mdelem md = l->md;
      /* Pointer comparison is OK for md_elems created from the same context.
       */
      if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_AUTHORITY)) {
        if (calld->have_host) {
          grpc_slice_unref_internal(exec_ctx, calld->host);
        }
        calld->host = grpc_slice_ref_internal(GRPC_MDVALUE(md));
        calld->have_host = true;
      } else if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_PATH)) {
        if (calld->have_method) {
          grpc_slice_unref_internal(exec_ctx, calld->method);
        }
        calld->method = grpc_slice_ref_internal(GRPC_MDVALUE(md));
        calld->have_method = true;
      }
    }
    if (calld->have_host) {
      grpc_error *cancel_error = set_cancel_func(elem, cancel_check_call_host);
      if (cancel_error != GRPC_ERROR_NONE) {
        grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, batch,
                                                           cancel_error);
      } else {
        char *call_host = grpc_slice_to_c_string(calld->host);
        batch->handler_private.extra_arg = elem;
        grpc_error *error = GRPC_ERROR_NONE;
        if (grpc_channel_security_connector_check_call_host(
                exec_ctx, chand->security_connector, call_host,
                chand->auth_context,
                GRPC_CLOSURE_INIT(&calld->closure, on_host_checked, batch,
                                  grpc_schedule_on_exec_ctx),
                &error)) {
          // Synchronous return; invoke on_host_checked() directly.
          on_host_checked(exec_ctx, batch, error);
          GRPC_ERROR_UNREF(error);
        }
        gpr_free(call_host);
      }
      GPR_TIMER_END("auth_start_transport_stream_op_batch", 0);
      return; /* early exit */
    }
  }

  /* pass control down the stack */
  grpc_call_next_op(exec_ctx, elem, batch);
  GPR_TIMER_END("auth_start_transport_stream_op_batch", 0);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  memset(calld, 0, sizeof(*calld));
  gpr_mu_init(&calld->security_context_mu);
  return GRPC_ERROR_NONE;
}

static void set_pollset_or_pollset_set(grpc_exec_ctx *exec_ctx,
                                       grpc_call_element *elem,
                                       grpc_polling_entity *pollent) {
  call_data *calld = elem->call_data;
  calld->pollent = pollent;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {
  call_data *calld = elem->call_data;
  grpc_credentials_mdelem_array_destroy(exec_ctx, &calld->md_array);
  grpc_call_credentials_unref(exec_ctx, calld->creds);
  if (calld->have_host) {
    grpc_slice_unref_internal(exec_ctx, calld->host);
  }
  if (calld->have_method) {
    grpc_slice_unref_internal(exec_ctx, calld->method);
  }
  reset_auth_metadata_context(&calld->auth_md_context);
  gpr_mu_destroy(&calld->security_context_mu);
  gpr_atm cancel_state = gpr_atm_acq_load(&calld->cancellation_state);
  grpc_error *cancel_error = GRPC_ERROR_NONE;
  grpc_closure *cancel_func = NULL;
  decode_cancel_state(cancel_state, &cancel_func, &cancel_error);
  GRPC_ERROR_UNREF(cancel_error);
}

/* Constructor for channel_data */
static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  grpc_security_connector *sc =
      grpc_security_connector_find_in_args(args->channel_args);
  if (sc == NULL) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Security connector missing from client auth filter args");
  }
  grpc_auth_context *auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  if (auth_context == NULL) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Auth context missing from client auth filter args");
  }

  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;

  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!args->is_last);

  /* initialize members */
  chand->security_connector =
      (grpc_channel_security_connector *)GRPC_SECURITY_CONNECTOR_REF(
          sc, "client_auth_filter");
  chand->auth_context =
      GRPC_AUTH_CONTEXT_REF(auth_context, "client_auth_filter");
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *chand = elem->channel_data;
  grpc_channel_security_connector *sc = chand->security_connector;
  if (sc != NULL) {
    GRPC_SECURITY_CONNECTOR_UNREF(exec_ctx, &sc->base, "client_auth_filter");
  }
  GRPC_AUTH_CONTEXT_UNREF(chand->auth_context, "client_auth_filter");
}

const grpc_channel_filter grpc_client_auth_filter = {
    auth_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "client-auth"};
