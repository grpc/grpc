//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/rbac/rbac_filter.h"

#include "src/core/ext/filters/rbac/rbac_service_config_parser.h"
#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

//
// RbacFilter::CallData
//

// CallData

grpc_error_handle RbacFilter::CallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  new (elem->call_data) CallData(elem, *args);
  return GRPC_ERROR_NONE;
}

void RbacFilter::CallData::Destroy(grpc_call_element* elem,
                                   const grpc_call_final_info* /*final_info*/,
                                   grpc_closure* /*then_schedule_closure*/) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

void RbacFilter::CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (op->recv_initial_metadata) {
    calld->recv_initial_metadata_ =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->original_recv_initial_metadata_ready_ =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready_;
  }
  // Chain to the next filter.
  grpc_call_next_op(elem, op);
}

RbacFilter::CallData::CallData(grpc_call_element* elem,
                               const grpc_call_element_args& args)
    : call_context_(args.context) {
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
}

void RbacFilter::CallData::RecvInitialMetadataReady(void* user_data,
                                                    grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (error == GRPC_ERROR_NONE) {
    // Fetch and apply the rbac policy from the service config.
    auto* service_config_call_data = static_cast<ServiceConfigCallData*>(
        calld->call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
    auto* method_params = static_cast<RbacMethodParsedConfig*>(
        service_config_call_data->GetMethodParsedConfig(
            RbacServiceConfigParser::ParserIndex()));
    if (method_params == nullptr) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("No RBAC policy found.");
    } else {
      RbacFilter* chand = static_cast<RbacFilter*>(elem->channel_data);
      auto* authorization_engine =
          method_params->authorization_engine(chand->index_);
      if (authorization_engine
              ->Evaluate(EvaluateArgs(calld->recv_initial_metadata_,
                                      &chand->per_channel_evaluate_args_))
              .type == AuthorizationEngine::Decision::Type::kDeny) {
        error =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unauthorized RPC rejected");
      }
    }
    if (error != GRPC_ERROR_NONE) {
      error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                                 GRPC_STATUS_PERMISSION_DENIED);
    }
  } else {
    GRPC_ERROR_REF(error);
  }
  grpc_closure* closure = calld->original_recv_initial_metadata_ready_;
  calld->original_recv_initial_metadata_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, error);
}

//
// RbacFilter
//

const grpc_channel_filter RbacFilter::kFilterVtable = {
    RbacFilter::CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(RbacFilter::CallData),
    RbacFilter::CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    RbacFilter::CallData::Destroy,
    sizeof(RbacFilter),
    RbacFilter::Init,
    RbacFilter::Destroy,
    grpc_channel_next_get_info,
    "rbac_filter",
};

RbacFilter::RbacFilter(size_t index,
                       EvaluateArgs::PerChannelArgs per_channel_evaluate_args)
    : index_(index),
      per_channel_evaluate_args_(std::move(per_channel_evaluate_args)) {}

grpc_error_handle RbacFilter::Init(grpc_channel_element* elem,
                                   grpc_channel_element_args* args) {
  GPR_ASSERT(elem->filter == &kFilterVtable);
  auto* auth_context = grpc_find_auth_context_in_args(args->channel_args);
  if (auth_context == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No auth context found");
  }
  if (args->optional_transport == nullptr) {
    // This should never happen since the transport is always set on the server
    // side.
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No transport configured");
  }
  new (elem->channel_data) RbacFilter(
      grpc_channel_stack_filter_instance_number(args->channel_stack, elem),
      EvaluateArgs::PerChannelArgs(
          auth_context, grpc_transport_get_endpoint(args->optional_transport)));
  return GRPC_ERROR_NONE;
}

void RbacFilter::Destroy(grpc_channel_element* elem) {
  auto* chand = static_cast<RbacFilter*>(elem->channel_data);
  chand->~RbacFilter();
}

void RbacFilterInit(void) { RbacServiceConfigParser::Register(); }

void RbacFilterShutdown(void) {}

}  // namespace grpc_core
