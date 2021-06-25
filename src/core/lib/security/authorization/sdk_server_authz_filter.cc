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

#include "src/core/lib/security/authorization/authorization_policy_provider.h"
#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/transport/transport.h"

namespace {

class ChannelData {
 public:
  ChannelData(
      grpc_core::RefCountedPtr<grpc_auth_context> auth_context,
      grpc_endpoint* endpoint,
      grpc_core::RefCountedPtr<grpc_authorization_policy_provider> provider)
      : auth_context_(std::move(auth_context)),
        channel_args_(grpc_core::EvaluateArgs::PerChannelArgs(
            auth_context_.get(), endpoint)),
        provider_(std::move(provider)) {}

  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  grpc_core::RefCountedPtr<grpc_auth_context> auth_context_;
  grpc_core::EvaluateArgs::PerChannelArgs channel_args_;
  grpc_core::RefCountedPtr<grpc_authorization_policy_provider> provider_;
};

class CallData {
 public:
  CallData(grpc_call_element* elem, const grpc_call_element_args& args)
      : call_combiner_(args.call_combiner) {
    GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                      elem, grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                      elem, grpc_schedule_on_exec_ctx);
  }

  ~CallData() { GRPC_ERROR_UNREF(recv_initial_metadata_error_); }

  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* /*final_info*/,
                      grpc_closure* /*ignored*/);

 private:
  static void RecvInitialMetadataReady(void* arg, grpc_error_handle error);
  static void RecvTrailingMetadataReady(void* user_data, grpc_error_handle err);

  grpc_core::CallCombiner* call_combiner_;
  grpc_transport_stream_op_batch* recv_initial_metadata_batch_;
  grpc_closure* original_recv_initial_metadata_ready_;
  grpc_closure recv_initial_metadata_ready_;
  grpc_error_handle recv_initial_metadata_error_ = GRPC_ERROR_NONE;
  grpc_closure* original_recv_trailing_metadata_ready_;
  grpc_closure recv_trailing_metadata_ready_;
  grpc_error_handle recv_trailing_metadata_error_;
  bool seen_recv_trailing_metadata_ready_ = false;
};

bool IsAuthorized(ChannelData* chand, grpc_transport_stream_op_batch* batch) {
  grpc_core::EvaluateArgs args(
      batch->payload->recv_initial_metadata.recv_initial_metadata,
      &chand->channel_args_);
  if (chand->provider_->deny_engine() != nullptr) {
    grpc_core::AuthorizationEngine::Decision decision =
        chand->provider_->deny_engine()->Evaluate(args);
    if (decision.type ==
        grpc_core::AuthorizationEngine::Decision::Type::kDeny) {
      return false;
    }
  }
  if (chand->provider_->allow_engine() != nullptr) {
    grpc_core::AuthorizationEngine::Decision decision =
        chand->provider_->allow_engine()->Evaluate(args);
    if (decision.type ==
        grpc_core::AuthorizationEngine::Decision::Type::kAllow) {
      return true;
    }
  }
  return false;
}

void CallData::RecvInitialMetadataReady(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
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
  } else {
    GRPC_ERROR_REF(error);
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
  grpc_core::Closure::Run(DEBUG_LOCATION, closure, error);
}

void CallData::RecvTrailingMetadataReady(void* user_data,
                                         grpc_error_handle err) {
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
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_recv_trailing_metadata_ready_, err);
}

grpc_error_handle ChannelData::Init(grpc_channel_element* elem,
                                    grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  grpc_auth_context* auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  GPR_ASSERT(auth_context != nullptr);
  grpc_transport* transport = args->optional_transport;
  GPR_ASSERT(transport != nullptr);
  grpc_endpoint* endpoint = grpc_transport_get_endpoint(transport);
  GPR_ASSERT(endpoint != nullptr);
  grpc_authorization_policy_provider* provider =
      grpc_channel_args_find_pointer<grpc_authorization_policy_provider>(
          args->channel_args, GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER);
  GPR_ASSERT(provider != nullptr);
  new (elem->channel_data)
      ChannelData(auth_context->Ref(), endpoint, provider->Ref());
  return GRPC_ERROR_NONE;
}

void ChannelData::Destroy(grpc_channel_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

void CallData::StartTransportStreamOpBatch(
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
  if (batch->recv_trailing_metadata) {
    calld->original_recv_trailing_metadata_ready_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready_;
  }
  grpc_call_next_op(elem, batch);
}

grpc_error_handle CallData::Init(grpc_call_element* elem,
                                 const grpc_call_element_args* args) {
  new (elem->call_data) CallData(elem, *args);
  return GRPC_ERROR_NONE;
}

void CallData::Destroy(grpc_call_element* elem,
                       const grpc_call_final_info* /*final_info*/,
                       grpc_closure* /*ignored*/) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

}  // namespace

const grpc_channel_filter SdkServerAuthzFilter = {
    CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(ChannelData),
    ChannelData::Init,
    ChannelData::Destroy,
    grpc_channel_next_get_info,
    "sdk-server-authz"};
