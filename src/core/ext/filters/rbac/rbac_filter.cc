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
#include "src/core/ext/service_config/service_config_call_data.h"
#include "src/core/lib/security/authorization/grpc_authorization_engine.h"

namespace grpc_core {

namespace {

class ChannelData {
 public:
  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  grpc_auth_context* auth_context() { return auth_context_.get(); }

  int index() const { return index_; }

 private:
  ChannelData(grpc_channel_element* elem, grpc_channel_element_args* args);

  RefCountedPtr<grpc_auth_context> auth_context_;
  // The index of this filter instance among instances of the same filter.
  int index_;
};

class CallData {
 public:
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* /* final_info */,
                      grpc_closure* /* then_schedule_closure */);
  static void StartTransportStreamOpBatch(grpc_call_element* elem,
                                          grpc_transport_stream_op_batch* op);

 private:
  CallData(grpc_call_element* elem, const grpc_call_element_args& args);
  ~CallData();
  static void RecvInitialMetadataReady(void* user_data,
                                       grpc_error_handle error);
  static void RecvTrailingMetadataReady(void* user_data,
                                        grpc_error_handle error);
  void MaybeResumeRecvTrailingMetadataReady();

  grpc_call_context_element* call_context_;
  Arena* arena_;
  CallCombiner* call_combiner_;
  // Overall error for the call
  grpc_error_handle error_ = GRPC_ERROR_NONE;
  // State for keeping track of recv_initial_metadata
  grpc_metadata_batch* recv_initial_metadata_ = nullptr;
  grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
  grpc_closure recv_initial_metadata_ready_;
  // State for keeping of track of recv_trailing_metadata
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  grpc_closure recv_trailing_metadata_ready_;
  grpc_error_handle recv_trailing_metadata_ready_error_ = GRPC_ERROR_NONE;
  bool seen_recv_trailing_metadata_ready_ = false;
  // Payload for access to recv_initial_metadata fields
  grpc_transport_stream_op_batch_payload* payload_ = nullptr;
};

// ChannelData

grpc_error_handle ChannelData::Init(grpc_channel_element* elem,
                                    grpc_channel_element_args* args) {
  GPR_ASSERT(elem->filter == &kRbacFilter);
  new (elem->channel_data) ChannelData(elem, args);
  return GRPC_ERROR_NONE;
}

void ChannelData::Destroy(grpc_channel_element* elem) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

ChannelData::ChannelData(grpc_channel_element* elem,
                         grpc_channel_element_args* args)
    : index_(grpc_channel_stack_filter_instance_number(args->channel_stack,
                                                       elem)) {
  grpc_auth_context* auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  GPR_ASSERT(auth_context != nullptr);
  auth_context_ = auth_context->Ref();
}

// CallData

grpc_error_handle CallData::Init(grpc_call_element* elem,
                                 const grpc_call_element_args* args) {
  new (elem->call_data) CallData(elem, *args);
  return GRPC_ERROR_NONE;
}

void CallData::Destroy(grpc_call_element* elem,
                       const grpc_call_final_info* /*final_info*/,
                       grpc_closure* /*then_schedule_closure*/) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

void CallData::StartTransportStreamOpBatch(grpc_call_element* elem,
                                           grpc_transport_stream_op_batch* op) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (op->recv_initial_metadata) {
    calld->recv_initial_metadata_ =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->original_recv_initial_metadata_ready_ =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready_;
    calld->payload_ = op->payload;
  }
  if (op->recv_trailing_metadata) {
    // We might generate errors on receiving initial metadata which we need to
    // bubble up through recv_trailing_metadata_ready
    calld->original_recv_trailing_metadata_ready_ =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready_;
  }
  // Chain to the next filter.
  grpc_call_next_op(elem, op);
}

CallData::CallData(grpc_call_element* elem, const grpc_call_element_args& args)
    : call_context_(args.context),
      arena_(args.arena),
      call_combiner_(args.call_combiner) {
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
}

CallData::~CallData() { GRPC_ERROR_UNREF(error_); }

namespace {

grpc_error_handle PrepareMetadataForAuthorization(
    const grpc_metadata_batch& source, uint32_t flags,
    grpc_metadata_batch* destination) {
  // The http-server filter removes the ':method' header from the metadata which
  // we need to add back here.
  grpc_metadata_batch_copy(&source, destination);
  grpc_mdelem method;
  if (flags & GRPC_INITIAL_METADATA_CACHEABLE_REQUEST) {
    method = GRPC_MDELEM_METHOD_GET;
  } else if (flags & GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST) {
    method = GRPC_MDELEM_METHOD_PUT;
  } else {
    method = GRPC_MDELEM_METHOD_POST;
  }
  return destination->Append(method);
}

}  // namespace

void CallData::RecvInitialMetadataReady(void* user_data,
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
    if (method_params != nullptr) {
      grpc_metadata_batch prepared_metadata(calld->arena_);
      calld->error_ = PrepareMetadataForAuthorization(
          *calld->payload_->recv_initial_metadata.recv_initial_metadata,
          *calld->payload_->recv_initial_metadata.recv_flags,
          &prepared_metadata);
      if (calld->error_ == GRPC_ERROR_NONE) {
        ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
        auto* authorization_engine =
            method_params->authorization_engine(chand->index());
        EvaluateArgs::PerChannelArgs per_channel_evaluate_args(
            chand->auth_context(),
            calld->payload_->recv_initial_metadata.local_address,
            reinterpret_cast<const char*>(gpr_atm_acq_load(
                calld->payload_->recv_initial_metadata.peer_string)));
        if (authorization_engine
                ->Evaluate(EvaluateArgs(&prepared_metadata,
                                        &per_channel_evaluate_args))
                .type == AuthorizationEngine::Decision::Type::kDeny) {
          calld->error_ =
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unauthorized RPC rejected");
        }
      }
    } else {
      calld->error_ =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("No RBAC policy found.");
    }
    if (calld->error_ != GRPC_ERROR_NONE) {
      calld->error_ =
          grpc_error_set_int(calld->error_, GRPC_ERROR_INT_GRPC_STATUS,
                             GRPC_STATUS_PERMISSION_DENIED);
      error = calld->error_;  // Does not take a ref
    }
  }
  calld->MaybeResumeRecvTrailingMetadataReady();
  grpc_closure* closure = calld->original_recv_initial_metadata_ready_;
  calld->original_recv_initial_metadata_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, GRPC_ERROR_REF(error));
}

void CallData::RecvTrailingMetadataReady(void* user_data,
                                         grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (calld->original_recv_initial_metadata_ready_ != nullptr) {
    calld->seen_recv_trailing_metadata_ready_ = true;
    calld->recv_trailing_metadata_ready_error_ = GRPC_ERROR_REF(error);
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "Deferring RecvTrailingMetadataReady until after "
                            "RecvInitialMetadataReady");
    return;
  }
  error = grpc_error_add_child(GRPC_ERROR_REF(error), calld->error_);
  calld->error_ = GRPC_ERROR_NONE;
  grpc_closure* closure = calld->original_recv_trailing_metadata_ready_;
  calld->original_recv_trailing_metadata_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, error);
}

void CallData::MaybeResumeRecvTrailingMetadataReady() {
  if (seen_recv_trailing_metadata_ready_) {
    seen_recv_trailing_metadata_ready_ = false;
    grpc_error_handle error = recv_trailing_metadata_ready_error_;
    recv_trailing_metadata_ready_error_ = GRPC_ERROR_NONE;
    GRPC_CALL_COMBINER_START(call_combiner_, &recv_trailing_metadata_ready_,
                             error, "Continuing RecvTrailingMetadataReady");
  }
}

}  // namespace

const grpc_channel_filter kRbacFilter = {
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
    "rbac_filter",
};

void RbacFilterInit(void) { RbacServiceConfigParser::Register(); }

void RbacFilterShutdown(void) {}

}  // namespace grpc_core
