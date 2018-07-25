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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/transport/auth_filters.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/static_metadata.h"

#define MAX_CREDENTIALS_METADATA_COUNT 4

namespace {
/* We can have a per-call credentials. */
struct call_data {
  gpr_arena* arena;
  grpc_call_stack* owning_call;
  grpc_call_combiner* call_combiner;
  grpc_call_credentials* creds;
  grpc_slice host;
  grpc_slice method;
  /* pollset{_set} bound to this call; if we need to make external
     network requests, they should be done under a pollset added to this
     pollset_set so that work can progress when this call wants work to progress
  */
  grpc_polling_entity* pollent;
  grpc_credentials_mdelem_array md_array;
  grpc_linked_mdelem md_links[MAX_CREDENTIALS_METADATA_COUNT];
  grpc_auth_metadata_context auth_md_context;
  grpc_closure async_result_closure;
  grpc_closure check_call_host_cancel_closure;
  grpc_closure get_request_metadata_cancel_closure;
};

/* We can have a per-channel credentials. */
struct channel_data {
  grpc_channel_security_connector* security_connector;
  grpc_auth_context* auth_context;
};
}  // namespace

void grpc_auth_metadata_context_reset(
    grpc_auth_metadata_context* auth_md_context) {
  if (auth_md_context->service_url != nullptr) {
    gpr_free(const_cast<char*>(auth_md_context->service_url));
    auth_md_context->service_url = nullptr;
  }
  if (auth_md_context->method_name != nullptr) {
    gpr_free(const_cast<char*>(auth_md_context->method_name));
    auth_md_context->method_name = nullptr;
  }
  GRPC_AUTH_CONTEXT_UNREF(
      (grpc_auth_context*)auth_md_context->channel_auth_context,
      "grpc_auth_metadata_context");
  auth_md_context->channel_auth_context = nullptr;
}

static void add_error(grpc_error** combined, grpc_error* error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*combined == GRPC_ERROR_NONE) {
    *combined = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Client auth metadata plugin error");
  }
  *combined = grpc_error_add_child(*combined, error);
}

static void on_credentials_metadata(void* arg, grpc_error* input_error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  grpc_call_element* elem =
      static_cast<grpc_call_element*>(batch->handler_private.extra_arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_auth_metadata_context_reset(&calld->auth_md_context);
  grpc_error* error = GRPC_ERROR_REF(input_error);
  if (error == GRPC_ERROR_NONE) {
    GPR_ASSERT(calld->md_array.size <= MAX_CREDENTIALS_METADATA_COUNT);
    GPR_ASSERT(batch->send_initial_metadata);
    grpc_metadata_batch* mdb =
        batch->payload->send_initial_metadata.send_initial_metadata;
    for (size_t i = 0; i < calld->md_array.size; ++i) {
      add_error(&error, grpc_metadata_batch_add_tail(
                            mdb, &calld->md_links[i],
                            GRPC_MDELEM_REF(calld->md_array.md[i])));
    }
  }
  if (error == GRPC_ERROR_NONE) {
    grpc_call_next_op(elem, batch);
  } else {
    error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                               GRPC_STATUS_UNAVAILABLE);
    grpc_transport_stream_op_batch_finish_with_failure(batch, error,
                                                       calld->call_combiner);
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "get_request_metadata");
}

void grpc_auth_metadata_context_build(
    const char* url_scheme, grpc_slice call_host, grpc_slice call_method,
    grpc_auth_context* auth_context,
    grpc_auth_metadata_context* auth_md_context) {
  char* service = grpc_slice_to_c_string(call_method);
  char* last_slash = strrchr(service, '/');
  char* method_name = nullptr;
  char* service_url = nullptr;
  grpc_auth_metadata_context_reset(auth_md_context);
  if (last_slash == nullptr) {
    gpr_log(GPR_ERROR, "No '/' found in fully qualified method name");
    service[0] = '\0';
    method_name = gpr_strdup("");
  } else if (last_slash == service) {
    method_name = gpr_strdup("");
  } else {
    *last_slash = '\0';
    method_name = gpr_strdup(last_slash + 1);
  }
  char* host_and_port = grpc_slice_to_c_string(call_host);
  if (url_scheme != nullptr && strcmp(url_scheme, GRPC_SSL_URL_SCHEME) == 0) {
    /* Remove the port if it is 443. */
    char* port_delimiter = strrchr(host_and_port, ':');
    if (port_delimiter != nullptr && strcmp(port_delimiter + 1, "443") == 0) {
      *port_delimiter = '\0';
    }
  }
  gpr_asprintf(&service_url, "%s://%s%s",
               url_scheme == nullptr ? "" : url_scheme, host_and_port, service);
  auth_md_context->service_url = service_url;
  auth_md_context->method_name = method_name;
  auth_md_context->channel_auth_context =
      GRPC_AUTH_CONTEXT_REF(auth_context, "grpc_auth_metadata_context");
  gpr_free(service);
  gpr_free(host_and_port);
}

static void cancel_get_request_metadata(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error != GRPC_ERROR_NONE) {
    grpc_call_credentials_cancel_get_request_metadata(
        calld->creds, &calld->md_array, GRPC_ERROR_REF(error));
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "cancel_get_request_metadata");
}

static void send_security_metadata(grpc_call_element* elem,
                                   grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_client_security_context* ctx =
      static_cast<grpc_client_security_context*>(
          batch->payload->context[GRPC_CONTEXT_SECURITY].value);
  grpc_call_credentials* channel_call_creds =
      chand->security_connector->request_metadata_creds;
  int call_creds_has_md = (ctx != nullptr) && (ctx->creds != nullptr);

  if (channel_call_creds == nullptr && !call_creds_has_md) {
    /* Skip sending metadata altogether. */
    grpc_call_next_op(elem, batch);
    return;
  }

  if (channel_call_creds != nullptr && call_creds_has_md) {
    calld->creds = grpc_composite_call_credentials_create(channel_call_creds,
                                                          ctx->creds, nullptr);
    if (calld->creds == nullptr) {
      grpc_transport_stream_op_batch_finish_with_failure(
          batch,
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "Incompatible credentials set on channel and call."),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAUTHENTICATED),
          calld->call_combiner);
      return;
    }
  } else {
    calld->creds = grpc_call_credentials_ref(
        call_creds_has_md ? ctx->creds : channel_call_creds);
  }

  grpc_auth_metadata_context_build(
      chand->security_connector->base.url_scheme, calld->host, calld->method,
      chand->auth_context, &calld->auth_md_context);

  GPR_ASSERT(calld->pollent != nullptr);
  GRPC_CALL_STACK_REF(calld->owning_call, "get_request_metadata");
  GRPC_CLOSURE_INIT(&calld->async_result_closure, on_credentials_metadata,
                    batch, grpc_schedule_on_exec_ctx);
  grpc_error* error = GRPC_ERROR_NONE;
  if (grpc_call_credentials_get_request_metadata(
          calld->creds, calld->pollent, calld->auth_md_context,
          &calld->md_array, &calld->async_result_closure, &error)) {
    // Synchronous return; invoke on_credentials_metadata() directly.
    on_credentials_metadata(batch, error);
    GRPC_ERROR_UNREF(error);
  } else {
    // Async return; register cancellation closure with call combiner.
    GRPC_CALL_STACK_REF(calld->owning_call, "cancel_get_request_metadata");
    grpc_call_combiner_set_notify_on_cancel(
        calld->call_combiner,
        GRPC_CLOSURE_INIT(&calld->get_request_metadata_cancel_closure,
                          cancel_get_request_metadata, elem,
                          grpc_schedule_on_exec_ctx));
  }
}

static void on_host_checked(void* arg, grpc_error* error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  grpc_call_element* elem =
      static_cast<grpc_call_element*>(batch->handler_private.extra_arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error == GRPC_ERROR_NONE) {
    send_security_metadata(elem, batch);
  } else {
    char* error_msg;
    char* host = grpc_slice_to_c_string(calld->host);
    gpr_asprintf(&error_msg, "Invalid host %s set in :authority metadata.",
                 host);
    gpr_free(host);
    grpc_transport_stream_op_batch_finish_with_failure(
        batch,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_UNAUTHENTICATED),
        calld->call_combiner);
    gpr_free(error_msg);
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "check_call_host");
}

static void cancel_check_call_host(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (error != GRPC_ERROR_NONE) {
    grpc_channel_security_connector_cancel_check_call_host(
        chand->security_connector, &calld->async_result_closure,
        GRPC_ERROR_REF(error));
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "cancel_check_call_host");
}

static void auth_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("auth_start_transport_stream_op_batch", 0);

  /* grab pointers to our data from the call element */
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);

  if (!batch->cancel_stream) {
    // TODO(hcaseyal): move this to init_call_elem once issue #15927 is
    // resolved.
    GPR_ASSERT(batch->payload->context != nullptr);
    if (batch->payload->context[GRPC_CONTEXT_SECURITY].value == nullptr) {
      batch->payload->context[GRPC_CONTEXT_SECURITY].value =
          grpc_client_security_context_create(calld->arena);
      batch->payload->context[GRPC_CONTEXT_SECURITY].destroy =
          grpc_client_security_context_destroy;
    }
    grpc_client_security_context* sec_ctx =
        static_cast<grpc_client_security_context*>(
            batch->payload->context[GRPC_CONTEXT_SECURITY].value);
    GRPC_AUTH_CONTEXT_UNREF(sec_ctx->auth_context, "client auth filter");
    sec_ctx->auth_context =
        GRPC_AUTH_CONTEXT_REF(chand->auth_context, "client_auth_filter");
  }

  if (batch->send_initial_metadata) {
    grpc_metadata_batch* metadata =
        batch->payload->send_initial_metadata.send_initial_metadata;
    if (metadata->idx.named.path != nullptr) {
      calld->method =
          grpc_slice_ref_internal(GRPC_MDVALUE(metadata->idx.named.path->md));
    }
    if (metadata->idx.named.authority != nullptr) {
      calld->host = grpc_slice_ref_internal(
          GRPC_MDVALUE(metadata->idx.named.authority->md));
      batch->handler_private.extra_arg = elem;
      GRPC_CALL_STACK_REF(calld->owning_call, "check_call_host");
      GRPC_CLOSURE_INIT(&calld->async_result_closure, on_host_checked, batch,
                        grpc_schedule_on_exec_ctx);
      char* call_host = grpc_slice_to_c_string(calld->host);
      grpc_error* error = GRPC_ERROR_NONE;
      if (grpc_channel_security_connector_check_call_host(
              chand->security_connector, call_host, chand->auth_context,
              &calld->async_result_closure, &error)) {
        // Synchronous return; invoke on_host_checked() directly.
        on_host_checked(batch, error);
        GRPC_ERROR_UNREF(error);
      } else {
        // Async return; register cancellation closure with call combiner.
        GRPC_CALL_STACK_REF(calld->owning_call, "cancel_check_call_host");
        grpc_call_combiner_set_notify_on_cancel(
            calld->call_combiner,
            GRPC_CLOSURE_INIT(&calld->check_call_host_cancel_closure,
                              cancel_check_call_host, elem,
                              grpc_schedule_on_exec_ctx));
      }
      gpr_free(call_host);
      return; /* early exit */
    }
  }

  /* pass control down the stack */
  grpc_call_next_op(elem, batch);
}

/* Constructor for call_data */
static grpc_error* init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->arena = args->arena;
  calld->owning_call = args->call_stack;
  calld->call_combiner = args->call_combiner;
  calld->host = grpc_empty_slice();
  calld->method = grpc_empty_slice();
  return GRPC_ERROR_NONE;
}

static void set_pollset_or_pollset_set(grpc_call_element* elem,
                                       grpc_polling_entity* pollent) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->pollent = pollent;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_credentials_mdelem_array_destroy(&calld->md_array);
  grpc_call_credentials_unref(calld->creds);
  grpc_slice_unref_internal(calld->host);
  grpc_slice_unref_internal(calld->method);
  grpc_auth_metadata_context_reset(&calld->auth_md_context);
}

/* Constructor for channel_data */
static grpc_error* init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  grpc_security_connector* sc =
      grpc_security_connector_find_in_args(args->channel_args);
  if (sc == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Security connector missing from client auth filter args");
  }
  grpc_auth_context* auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  if (auth_context == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Auth context missing from client auth filter args");
  }

  /* grab pointers to our data from the channel element */
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);

  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!args->is_last);

  /* initialize members */
  chand->security_connector =
      reinterpret_cast<grpc_channel_security_connector*>(
          GRPC_SECURITY_CONNECTOR_REF(sc, "client_auth_filter"));
  chand->auth_context =
      GRPC_AUTH_CONTEXT_REF(auth_context, "client_auth_filter");
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element* elem) {
  /* grab pointers to our data from the channel element */
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_channel_security_connector* sc = chand->security_connector;
  if (sc != nullptr) {
    GRPC_SECURITY_CONNECTOR_UNREF(&sc->base, "client_auth_filter");
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
    grpc_channel_next_get_info,
    "client-auth"};
