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

#include <string.h>

#include <string>

#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"

#define MAX_CREDENTIALS_METADATA_COUNT 4

namespace {

/* We can have a per-channel credentials. */
struct channel_data {
  channel_data(grpc_channel_security_connector* security_connector,
               grpc_auth_context* auth_context)
      : security_connector(
            security_connector->Ref(DEBUG_LOCATION, "client_auth_filter")),
        auth_context(auth_context->Ref(DEBUG_LOCATION, "client_auth_filter")) {}
  ~channel_data() {
    security_connector.reset(DEBUG_LOCATION, "client_auth_filter");
    auth_context.reset(DEBUG_LOCATION, "client_auth_filter");
  }

  grpc_core::RefCountedPtr<grpc_channel_security_connector> security_connector;
  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
};

/* We can have a per-call credentials. */
struct call_data {
  call_data(grpc_call_element* elem, const grpc_call_element_args& args)
      : owning_call(args.call_stack), call_combiner(args.call_combiner) {
    host.Init();
    method.Init();
    md_array.Init();
    channel_data* chand = static_cast<channel_data*>(elem->channel_data);
    GPR_ASSERT(args.context != nullptr);
    if (args.context[GRPC_CONTEXT_SECURITY].value == nullptr) {
      args.context[GRPC_CONTEXT_SECURITY].value =
          grpc_client_security_context_create(args.arena, /*creds=*/nullptr);
      args.context[GRPC_CONTEXT_SECURITY].destroy =
          grpc_client_security_context_destroy;
    }
    grpc_client_security_context* sec_ctx =
        static_cast<grpc_client_security_context*>(
            args.context[GRPC_CONTEXT_SECURITY].value);
    sec_ctx->auth_context.reset(DEBUG_LOCATION, "client_auth_filter");
    sec_ctx->auth_context =
        chand->auth_context->Ref(DEBUG_LOCATION, "client_auth_filter");
  }

  // This method is technically the dtor of this class. However, since
  // `get_request_metadata_cancel_closure` can run in parallel to
  // `destroy_call_elem`, we cannot call the dtor in them. Otherwise,
  // fields will be accessed after calling dtor, and msan correctly complains
  // that the memory is not initialized.
  void destroy() {
    md_array.Destroy();
    creds.reset();
    grpc_auth_metadata_context_reset(&auth_md_context);
    host.Destroy();
    method.Destroy();
  }

  grpc_call_stack* owning_call;
  grpc_core::CallCombiner* call_combiner;
  grpc_core::RefCountedPtr<grpc_call_credentials> creds;
  grpc_core::ManualConstructor<grpc_core::Slice> host;
  grpc_core::ManualConstructor<grpc_core::Slice> method;
  /* pollset{_set} bound to this call; if we need to make external
     network requests, they should be done under a pollset added to this
     pollset_set so that work can progress when this call wants work to progress
  */
  grpc_polling_entity* pollent = nullptr;
  grpc_core::ManualConstructor<grpc_core::CredentialsMetadataArray> md_array;
  grpc_auth_metadata_context auth_md_context =
      grpc_auth_metadata_context();  // Zero-initialize the C struct.
  grpc_closure async_result_closure;
  grpc_closure check_call_host_cancel_closure;
  grpc_closure get_request_metadata_cancel_closure;
};

}  // namespace

void grpc_auth_metadata_context_copy(grpc_auth_metadata_context* from,
                                     grpc_auth_metadata_context* to) {
  grpc_auth_metadata_context_reset(to);
  to->channel_auth_context = from->channel_auth_context;
  if (to->channel_auth_context != nullptr) {
    const_cast<grpc_auth_context*>(to->channel_auth_context)
        ->Ref(DEBUG_LOCATION, "grpc_auth_metadata_context_copy")
        .release();
  }
  to->service_url = gpr_strdup(from->service_url);
  to->method_name = gpr_strdup(from->method_name);
}

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
  if (auth_md_context->channel_auth_context != nullptr) {
    const_cast<grpc_auth_context*>(auth_md_context->channel_auth_context)
        ->Unref(DEBUG_LOCATION, "grpc_auth_metadata_context");
    auth_md_context->channel_auth_context = nullptr;
  }
}

static void add_error(grpc_error_handle* combined, grpc_error_handle error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*combined == GRPC_ERROR_NONE) {
    *combined = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Client auth metadata plugin error");
  }
  *combined = grpc_error_add_child(*combined, error);
}

static void on_credentials_metadata(void* arg, grpc_error_handle input_error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  grpc_call_element* elem =
      static_cast<grpc_call_element*>(batch->handler_private.extra_arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_auth_metadata_context_reset(&calld->auth_md_context);
  grpc_error_handle error = GRPC_ERROR_REF(input_error);
  if (error == GRPC_ERROR_NONE) {
    GPR_ASSERT(calld->md_array->size() <= MAX_CREDENTIALS_METADATA_COUNT);
    GPR_ASSERT(batch->send_initial_metadata);
    grpc_metadata_batch* mdb =
        batch->payload->send_initial_metadata.send_initial_metadata;
    for (const auto& md : *calld->md_array) {
      mdb->Append(
          md.first.as_string_view(), md.second.Ref(),
          [&](absl::string_view error_message, const grpc_core::Slice& value) {
            add_error(&error, GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
                                  "on_credentials_metadata: ", error_message,
                                  ": ", md.first.as_string_view(), ": ",
                                  value.as_string_view())));
          });
    }
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
    const char* url_scheme, const grpc_slice& call_host,
    const grpc_slice& call_method, grpc_auth_context* auth_context,
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
      auth_context == nullptr
          ? nullptr
          : auth_context->Ref(DEBUG_LOCATION, "grpc_auth_metadata_context")
                .release();
  gpr_free(service);
  gpr_free(host_and_port);
}

static void cancel_get_request_metadata(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error != GRPC_ERROR_NONE) {
    calld->creds->cancel_get_request_metadata(&*calld->md_array,
                                              GRPC_ERROR_REF(error));
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "cancel_get_request_metadata");
}

static grpc_security_level convert_security_level_string_to_enum(
    const char* security_level) {
  if (strcmp(security_level, "TSI_INTEGRITY_ONLY") == 0) {
    return GRPC_INTEGRITY_ONLY;
  } else if (strcmp(security_level, "TSI_PRIVACY_AND_INTEGRITY") == 0) {
    return GRPC_PRIVACY_AND_INTEGRITY;
  }
  return GRPC_SECURITY_NONE;
}

bool grpc_check_security_level(grpc_security_level channel_level,
                               grpc_security_level call_cred_level) {
  return static_cast<int>(channel_level) >= static_cast<int>(call_cred_level);
}

static void send_security_metadata(grpc_call_element* elem,
                                   grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_client_security_context* ctx =
      static_cast<grpc_client_security_context*>(
          batch->payload->context[GRPC_CONTEXT_SECURITY].value);
  grpc_call_credentials* channel_call_creds =
      chand->security_connector->mutable_request_metadata_creds();
  int call_creds_has_md = (ctx != nullptr) && (ctx->creds != nullptr);

  if (channel_call_creds == nullptr && !call_creds_has_md) {
    /* Skip sending metadata altogether. */
    grpc_call_next_op(elem, batch);
    return;
  }

  if (channel_call_creds != nullptr && call_creds_has_md) {
    calld->creds = grpc_core::RefCountedPtr<grpc_call_credentials>(
        grpc_composite_call_credentials_create(channel_call_creds,
                                               ctx->creds.get(), nullptr));
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
    calld->creds =
        call_creds_has_md ? ctx->creds->Ref() : channel_call_creds->Ref();
  }

  /* Check security level of call credential and channel, and do not send
   * metadata if the check fails. */
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      chand->auth_context.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr) {
    grpc_transport_stream_op_batch_finish_with_failure(
        batch,
        grpc_error_set_int(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Established channel does not have an auth property "
                "representing a security level."),
            GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAUTHENTICATED),
        calld->call_combiner);
    return;
  }
  grpc_security_level call_cred_security_level =
      calld->creds->min_security_level();
  int is_security_level_ok = grpc_check_security_level(
      convert_security_level_string_to_enum(prop->value),
      call_cred_security_level);
  if (!is_security_level_ok) {
    grpc_transport_stream_op_batch_finish_with_failure(
        batch,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "Established channel does not have a sufficient "
                               "security level to transfer call credential."),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_UNAUTHENTICATED),
        calld->call_combiner);
    return;
  }

  grpc_auth_metadata_context_build(
      chand->security_connector->url_scheme(), calld->host->c_slice(),
      calld->method->c_slice(), chand->auth_context.get(),
      &calld->auth_md_context);

  GPR_ASSERT(calld->pollent != nullptr);
  GRPC_CALL_STACK_REF(calld->owning_call, "get_request_metadata");
  GRPC_CLOSURE_INIT(&calld->async_result_closure, on_credentials_metadata,
                    batch, grpc_schedule_on_exec_ctx);
  grpc_error_handle error = GRPC_ERROR_NONE;
  if (calld->creds->get_request_metadata(
          calld->pollent, calld->auth_md_context, &*calld->md_array,
          &calld->async_result_closure, &error)) {
    // Synchronous return; invoke on_credentials_metadata() directly.
    on_credentials_metadata(batch, error);
    GRPC_ERROR_UNREF(error);
  } else {
    // Async return; register cancellation closure with call combiner.
    // TODO(yashykt): We would not need this ref if call combiners used
    // Closure::Run() instead of ExecCtx::Run()
    GRPC_CALL_STACK_REF(calld->owning_call, "cancel_get_request_metadata");
    calld->call_combiner->SetNotifyOnCancel(GRPC_CLOSURE_INIT(
        &calld->get_request_metadata_cancel_closure,
        cancel_get_request_metadata, elem, grpc_schedule_on_exec_ctx));
  }
}

static void on_host_checked(void* arg, grpc_error_handle error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  grpc_call_element* elem =
      static_cast<grpc_call_element*>(batch->handler_private.extra_arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error == GRPC_ERROR_NONE) {
    send_security_metadata(elem, batch);
  } else {
    grpc_transport_stream_op_batch_finish_with_failure(
        batch,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
                               "Invalid host ", calld->host->as_string_view(),
                               " set in :authority metadata.")),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_UNAUTHENTICATED),
        calld->call_combiner);
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "check_call_host");
}

static void cancel_check_call_host(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (error != GRPC_ERROR_NONE) {
    chand->security_connector->cancel_check_call_host(
        &calld->async_result_closure, GRPC_ERROR_REF(error));
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "cancel_check_call_host");
}

static void client_auth_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("auth_start_transport_stream_op_batch", 0);

  /* grab pointers to our data from the call element */
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);

  if (batch->send_initial_metadata) {
    grpc_metadata_batch* metadata =
        batch->payload->send_initial_metadata.send_initial_metadata;
    if (metadata->get_pointer(grpc_core::HttpPathMetadata()) != nullptr) {
      *calld->method =
          metadata->get_pointer(grpc_core::HttpPathMetadata())->Ref();
    }
    if (metadata->get_pointer(grpc_core::HttpAuthorityMetadata()) != nullptr) {
      *calld->host =
          metadata->get_pointer(grpc_core::HttpAuthorityMetadata())->Ref();
      batch->handler_private.extra_arg = elem;
      GRPC_CALL_STACK_REF(calld->owning_call, "check_call_host");
      GRPC_CLOSURE_INIT(&calld->async_result_closure, on_host_checked, batch,
                        grpc_schedule_on_exec_ctx);
      absl::string_view call_host = calld->host->as_string_view();
      grpc_error_handle error = GRPC_ERROR_NONE;
      if (chand->security_connector->check_call_host(
              call_host, chand->auth_context.get(),
              &calld->async_result_closure, &error)) {
        // Synchronous return; invoke on_host_checked() directly.
        on_host_checked(batch, error);
        GRPC_ERROR_UNREF(error);
      } else {
        // Async return; register cancellation closure with call combiner.
        // TODO(yashykt): We would not need this ref if call combiners used
        // Closure::Run() instead of ExecCtx::Run()
        GRPC_CALL_STACK_REF(calld->owning_call, "cancel_check_call_host");
        calld->call_combiner->SetNotifyOnCancel(GRPC_CLOSURE_INIT(
            &calld->check_call_host_cancel_closure, cancel_check_call_host,
            elem, grpc_schedule_on_exec_ctx));
      }
      return; /* early exit */
    }
  }

  /* pass control down the stack */
  grpc_call_next_op(elem, batch);
}

/* Constructor for call_data */
static grpc_error_handle client_auth_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  new (elem->call_data) call_data(elem, *args);
  return GRPC_ERROR_NONE;
}

static void client_auth_set_pollset_or_pollset_set(
    grpc_call_element* elem, grpc_polling_entity* pollent) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->pollent = pollent;
}

/* Destructor for call_data */
static void client_auth_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->destroy();
}

/* Constructor for channel_data */
static grpc_error_handle client_auth_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!args->is_last);
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
  new (elem->channel_data) channel_data(
      static_cast<grpc_channel_security_connector*>(sc), auth_context);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void client_auth_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->~channel_data();
}

const grpc_channel_filter grpc_client_auth_filter = {
    client_auth_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    client_auth_init_call_elem,
    client_auth_set_pollset_or_pollset_set,
    client_auth_destroy_call_elem,
    sizeof(channel_data),
    client_auth_init_channel_elem,
    client_auth_destroy_channel_elem,
    grpc_channel_next_get_info,
    "client-auth"};
