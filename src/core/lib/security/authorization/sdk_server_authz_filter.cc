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
  grpc_endpoint* endpoint = nullptr;
  if (transport != nullptr) {
    endpoint = grpc_transport_get_endpoint(transport);
  }
  grpc_authorization_policy_provider* provider =
      grpc_channel_args_find_pointer<grpc_authorization_policy_provider>(
          args->channel_args, GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER);
  GPR_ASSERT(provider != nullptr);
  new (elem->channel_data) SdkServerAuthzFilter(
      auth_context != nullptr ? auth_context->Ref() : nullptr, endpoint,
      provider->Ref());
  return GRPC_ERROR_NONE;
}

void SdkServerAuthzFilter::Destroy(grpc_channel_element* elem) {
  SdkServerAuthzFilter* chand =
      static_cast<SdkServerAuthzFilter*>(elem->channel_data);
  chand->~SdkServerAuthzFilter();
}

SdkServerAuthzFilter::CallData::CallData(grpc_call_element* elem,
                                         const grpc_call_element_args& args)
    : call_combiner_(args.call_combiner) {
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
}

SdkServerAuthzFilter::CallData::~CallData() {
  GRPC_ERROR_UNREF(recv_initial_metadata_error_);
}

void SdkServerAuthzFilter::CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (batch->recv_initial_metadata) {
    // Inject our callback.
    calld->recv_initial_metadata_batch_ = batch;
    calld->original_recv_initial_metadata_ready_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready_;
  }
  grpc_call_next_op(elem, batch);
}

grpc_error_handle SdkServerAuthzFilter::CallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  new (elem->call_data) CallData(elem, *args);
  return GRPC_ERROR_NONE;
}

void SdkServerAuthzFilter::CallData::Destroy(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

bool SdkServerAuthzFilter::CallData::IsAuthorized(
    SdkServerAuthzFilter* chand, grpc_transport_stream_op_batch* batch) {
  EvaluateArgs args(batch->payload->recv_initial_metadata.recv_initial_metadata,
                    &chand->per_channel_evaluate_args_);
  RefCountedPtr<AuthorizationEngine> deny_engine =
      chand->provider_->engines().deny_engine;
  if (deny_engine != nullptr) {
    AuthorizationEngine::Decision decision = deny_engine->Evaluate(args);
    if (decision.type == AuthorizationEngine::Decision::Type::kDeny) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
        gpr_log(GPR_INFO, "Request denied. Matching policy name: %s.",
                decision.matching_policy_name.c_str());
      }
      return false;
    }
  }
  RefCountedPtr<AuthorizationEngine> allow_engine =
      chand->provider_->engines().allow_engine;
  if (allow_engine != nullptr) {
    AuthorizationEngine::Decision decision = allow_engine->Evaluate(args);
    if (decision.type == AuthorizationEngine::Decision::Type::kAllow) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
        gpr_log(GPR_INFO, "Request allowed. Matching policy name: %s.",
                decision.matching_policy_name.c_str());
      }
      return true;
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
    gpr_log(GPR_INFO, "Request denied. No match found.");
  }
  return false;
}

void SdkServerAuthzFilter::CallData::RecvInitialMetadataReady(
    void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  SdkServerAuthzFilter* chand =
      static_cast<SdkServerAuthzFilter*>(elem->channel_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  grpc_transport_stream_op_batch* batch = calld->recv_initial_metadata_batch_;
  if (error == GRPC_ERROR_NONE) {
    if (!IsAuthorized(chand, batch)) {
      error = grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                     "Unauthorized RPC request rejected."),
                                 GRPC_ERROR_INT_GRPC_STATUS,
                                 GRPC_STATUS_PERMISSION_DENIED);
      calld->recv_initial_metadata_error_ = GRPC_ERROR_REF(error);
    }
  }
  grpc_closure* closure = calld->original_recv_initial_metadata_ready_;
  calld->original_recv_initial_metadata_ready_ = nullptr;
  if (calld->seen_recv_trailing_metadata_ready_) {
    calld->seen_recv_trailing_metadata_ready_ = false;
    GRPC_CALL_COMBINER_START(calld->call_combiner_,
                             &calld->recv_trailing_metadata_ready_,
                             calld->recv_trailing_metadata_error_,
                             "continue recv_trailing_metadata_ready");
  }
  Closure::Run(DEBUG_LOCATION, closure, GRPC_ERROR_REF(error));
}

void SdkServerAuthzFilter::CallData::RecvTrailingMetadataReady(
    void* user_data, grpc_error_handle err) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (calld->original_recv_initial_metadata_ready_ != nullptr) {
    calld->recv_trailing_metadata_error_ = GRPC_ERROR_REF(err);
    calld->seen_recv_trailing_metadata_ready_ = true;
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "deferring recv_trailing_metadata_ready until "
                            "after recv_initial_metadata_ready");
    return;
  }
  err = grpc_error_add_child(
      GRPC_ERROR_REF(err), GRPC_ERROR_REF(calld->recv_initial_metadata_error_));
  Closure::Run(DEBUG_LOCATION, calld->original_recv_trailing_metadata_ready_,
               err);
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
