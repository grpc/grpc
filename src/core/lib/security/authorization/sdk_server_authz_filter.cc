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

#include "src/core/lib/security/authorization/sdk_server_authz_filter.h"

#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

TraceFlag grpc_sdk_authz_trace(false, "sdk_authz");

SdkServerAuthzFilter::SdkServerAuthzFilter(
    RefCountedPtr<grpc_auth_context> auth_context, grpc_endpoint* endpoint,
    RefCountedPtr<grpc_authorization_policy_provider> provider)
    : auth_context_(std::move(auth_context)),
      per_channel_evaluate_args_(auth_context_.get(), endpoint),
      provider_(std::move(provider)) {}

grpc_error_handle SdkServerAuthzFilter::Init(grpc_channel_element* elem,
                                             grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  grpc_auth_context* auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  grpc_transport* transport = args->optional_transport;
  if (transport == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to get transport");
  }
  grpc_endpoint* endpoint = grpc_transport_get_endpoint(transport);
  grpc_authorization_policy_provider* provider =
      grpc_channel_args_find_pointer<grpc_authorization_policy_provider>(
          args->channel_args, GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER);
  if (provider == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Failed to get authorization provider");
  }
  new (elem->channel_data) SdkServerAuthzFilter(
      auth_context != nullptr ? auth_context->Ref() : nullptr, endpoint,
      provider->Ref());
  return GRPC_ERROR_NONE;
}

void SdkServerAuthzFilter::Destroy(grpc_channel_element* elem) {
  auto* chand = static_cast<SdkServerAuthzFilter*>(elem->channel_data);
  chand->~SdkServerAuthzFilter();
}

SdkServerAuthzFilter::CallData::CallData(grpc_call_element* elem) {
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
}

void SdkServerAuthzFilter::CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  if (batch->recv_initial_metadata) {
    // Inject our callback.
    calld->recv_initial_metadata_batch_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata;
    calld->original_recv_initial_metadata_ready_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready_;
  }
  grpc_call_next_op(elem, batch);
}

grpc_error_handle SdkServerAuthzFilter::CallData::Init(
    grpc_call_element* elem, const grpc_call_element_args*) {
  new (elem->call_data) CallData(elem);
  return GRPC_ERROR_NONE;
}

void SdkServerAuthzFilter::CallData::Destroy(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

bool SdkServerAuthzFilter::CallData::IsAuthorized(SdkServerAuthzFilter* chand) {
  EvaluateArgs args(recv_initial_metadata_batch_,
                    &chand->per_channel_evaluate_args_);
  AuthorizationEngines engines = chand->provider_->engines();
  if (engines.deny_engine != nullptr) {
    AuthorizationEngine::Decision decision =
        engines.deny_engine->Evaluate(args);
    if (decision.type == AuthorizationEngine::Decision::Type::kDeny) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
        gpr_log(GPR_INFO,
                "Request denied. matching_policy_name=%s chand=%p calld=%p",
                decision.matching_policy_name.c_str(), chand, this);
      }
      return false;
    }
  }
  if (engines.allow_engine != nullptr) {
    AuthorizationEngine::Decision decision =
        engines.allow_engine->Evaluate(args);
    if (decision.type == AuthorizationEngine::Decision::Type::kAllow) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
        gpr_log(GPR_INFO,
                "Request allowed. matching_policy_name=%s chand=%p calld=%p",
                decision.matching_policy_name.c_str(), chand, this);
      }
      return true;
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
    gpr_log(GPR_INFO, "Request denied. No match found. chand=%p calld=%p",
            chand, this);
  }
  return false;
}

void SdkServerAuthzFilter::CallData::RecvInitialMetadataReady(
    void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  auto* chand = static_cast<SdkServerAuthzFilter*>(elem->channel_data);
  auto* calld = static_cast<CallData*>(elem->call_data);
  if (error == GRPC_ERROR_NONE) {
    if (!calld->IsAuthorized(chand)) {
      error = grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                     "Unauthorized RPC request rejected."),
                                 GRPC_ERROR_INT_GRPC_STATUS,
                                 GRPC_STATUS_PERMISSION_DENIED);
    }
  } else {
    GRPC_ERROR_REF(error);
  }
  grpc_closure* closure = calld->original_recv_initial_metadata_ready_;
  calld->original_recv_initial_metadata_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, error);
}

const grpc_channel_filter SdkServerAuthzFilter::kFilterVtable = {
    SdkServerAuthzFilter::CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(SdkServerAuthzFilter::CallData),
    SdkServerAuthzFilter::CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    SdkServerAuthzFilter::CallData::Destroy,
    sizeof(SdkServerAuthzFilter),
    SdkServerAuthzFilter::Init,
    SdkServerAuthzFilter::Destroy,
    grpc_channel_next_get_info,
    "sdk-server-authz"};

}  // namespace grpc_core
