// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/authorization/authorization_filter.h"

#include "src/core/lib/security/authorization/authorization_policy_provider.h"
#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/transport/transport.h"

namespace {

struct channel_data {
  channel_data(grpc_auth_context* auth_context, grpc_endpoint* endpoint,
               grpc_authorization_policy_provider* provider)
      : auth_context(auth_context->Ref()),
        provider(provider->Ref()),
        channel_args(absl::make_unique<grpc_core::EvaluateArgs::PerChannelArgs>(
            this->auth_context.get(), endpoint)) {}

  ~channel_data() {
    auth_context.reset(DEBUG_LOCATION, "sdk_server_authz_filter");
  }

  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
  grpc_core::RefCountedPtr<grpc_authorization_policy_provider> provider;
  std::unique_ptr<grpc_core::EvaluateArgs::PerChannelArgs> channel_args;
};

static void recv_initial_metadata_ready(void* arg, grpc_error_handle error);
static void recv_trailing_metadata_ready(void* user_data,
                                         grpc_error_handle err);

struct call_data {
  call_data(grpc_call_element* elem, const grpc_call_element_args& args)
      : call_combiner(args.call_combiner), owning_call(args.call_stack) {
    GRPC_CLOSURE_INIT(&recv_initial_metadata_ready,
                      ::recv_initial_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready,
                      ::recv_trailing_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
    // Create server security context.  Set its auth context from channel
    // data and save it in the call context.
    grpc_server_security_context* server_ctx =
        grpc_server_security_context_create(args.arena);
    channel_data* chand = static_cast<channel_data*>(elem->channel_data);
    server_ctx->auth_context = chand->auth_context->Ref(
        DEBUG_LOCATION, "sdk_server_authz_filter_call");
    if (args.context[GRPC_CONTEXT_SECURITY].value != nullptr) {
      args.context[GRPC_CONTEXT_SECURITY].destroy(
          args.context[GRPC_CONTEXT_SECURITY].value);
    }
    args.context[GRPC_CONTEXT_SECURITY].value = server_ctx;
    args.context[GRPC_CONTEXT_SECURITY].destroy =
        grpc_server_security_context_destroy;
  }

  ~call_data() { GRPC_ERROR_UNREF(recv_initial_metadata_error); }

  grpc_core::CallCombiner* call_combiner;
  grpc_call_stack* owning_call;
  grpc_transport_stream_op_batch* recv_initial_metadata_batch;
  grpc_closure* original_recv_initial_metadata_ready;
  grpc_closure recv_initial_metadata_ready;
  grpc_error_handle recv_initial_metadata_error = GRPC_ERROR_NONE;
  grpc_closure* original_recv_trailing_metadata_ready;
  grpc_closure recv_trailing_metadata_ready;
  grpc_error_handle recv_trailing_metadata_error;
  bool seen_recv_trailing_metadata_ready = false;
};

grpc_authorization_policy_provider* grpc_authorization_policy_provider_from_arg(
    const grpc_arg* arg) {
  if (strcmp(arg->key, GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER) != 0) {
    return nullptr;
  }
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER);
    return nullptr;
  }
  return static_cast<grpc_authorization_policy_provider*>(arg->value.pointer.p);
}

grpc_authorization_policy_provider*
grpc_find_authorization_policy_provider_in_args(const grpc_channel_args* args) {
  size_t i;
  if (args == nullptr) return nullptr;
  for (i = 0; i < args->num_args; i++) {
    grpc_authorization_policy_provider* p =
        grpc_authorization_policy_provider_from_arg(&args->args[i]);
    if (p != nullptr) return p;
  }
  return nullptr;
}

static void recv_initial_metadata_ready(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_transport_stream_op_batch* batch = calld->recv_initial_metadata_batch;
  if (error == GRPC_ERROR_NONE) {
    if (chand->provider != nullptr) {
      grpc_core::EvaluateArgs args(
          batch->payload->recv_initial_metadata.recv_initial_metadata,
          chand->channel_args.get());
      if (chand->provider->deny_engine() != nullptr) {
        grpc_core::AuthorizationEngine::Decision decision =
            chand->provider->deny_engine()->Evaluate(args);
        if (decision.type ==
            grpc_core::AuthorizationEngine::Decision::Type::kDeny) {
          error = grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                         "Unauthorized RPC request rejected."),
                                     GRPC_ERROR_INT_GRPC_STATUS,
                                     GRPC_STATUS_PERMISSION_DENIED);
          calld->recv_initial_metadata_error = GRPC_ERROR_REF(error);
        }
      }
      if (error == GRPC_ERROR_NONE) {
        if (chand->provider->allow_engine() != nullptr) {
          grpc_core::AuthorizationEngine::Decision decision =
              chand->provider->allow_engine()->Evaluate(args);
          if (decision.type ==
              grpc_core::AuthorizationEngine::Decision::Type::kDeny) {
            error = grpc_error_set_int(
                GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Unauthorized RPC request rejected."),
                GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_PERMISSION_DENIED);
            calld->recv_initial_metadata_error = GRPC_ERROR_REF(error);
          }
        }
      }
    }
  } else {
    GRPC_ERROR_REF(error);
  }
  grpc_closure* closure = calld->original_recv_initial_metadata_ready;
  calld->original_recv_initial_metadata_ready = nullptr;
  if (calld->seen_recv_trailing_metadata_ready) {
    calld->seen_recv_trailing_metadata_ready = false;
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &calld->recv_trailing_metadata_ready,
                             calld->recv_trailing_metadata_error,
                             "continue recv_trailing_metadata_ready");
  }
  grpc_core::Closure::Run(DEBUG_LOCATION, closure, error);
}

static void recv_trailing_metadata_ready(void* user_data,
                                         grpc_error_handle err) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->original_recv_initial_metadata_ready != nullptr) {
    calld->recv_trailing_metadata_error = GRPC_ERROR_REF(err);
    calld->seen_recv_trailing_metadata_ready = true;
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "deferring recv_trailing_metadata_ready until "
                            "after recv_initial_metadata_ready");
    return;
  }
  err = grpc_error_add_child(
      GRPC_ERROR_REF(err), GRPC_ERROR_REF(calld->recv_initial_metadata_error));
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_recv_trailing_metadata_ready, err);
}

static void server_authz_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (batch->recv_initial_metadata) {
    // Inject our callback.
    calld->recv_initial_metadata_batch = batch;
    calld->original_recv_initial_metadata_ready =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready;
  }
  if (batch->recv_trailing_metadata) {
    calld->original_recv_trailing_metadata_ready =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready;
  }
  grpc_call_next_op(elem, batch);
}

// Constructor for call_data
static grpc_error_handle server_authz_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  new (elem->call_data) call_data(elem, *args);
  return GRPC_ERROR_NONE;
}

// Destructor for call_data
static void server_authz_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->~call_data();
}

// Constructor for channel_data
static grpc_error_handle server_authz_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  grpc_auth_context* auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  if (auth_context == nullptr) {
    grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No authorization context found. This might be a TRANSIENT failure due "
        "to certificates not having been loaded yet.");
    gpr_log(GPR_DEBUG, "%s", grpc_error_std_string(error).c_str());
    return error;
  }
  grpc_transport* transport = args->optional_transport;
  GPR_ASSERT(transport != nullptr);
  grpc_endpoint* endpoint = grpc_transport_get_endpoint(transport);
  GPR_ASSERT(endpoint != nullptr);
  grpc_authorization_policy_provider* provider =
      grpc_find_authorization_policy_provider_in_args(args->channel_args);
  new (elem->channel_data) channel_data(auth_context, endpoint, provider);
  return GRPC_ERROR_NONE;
}

// Destructor for channel data
static void server_authz_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->~channel_data();
}

}  // namespace

const grpc_channel_filter grpc_sdk_server_authz_filter = {
    server_authz_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    server_authz_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    server_authz_destroy_call_elem,
    sizeof(channel_data),
    server_authz_init_channel_elem,
    server_authz_destroy_channel_elem,
    grpc_channel_next_get_info,
    "sdk-server-authz"};
