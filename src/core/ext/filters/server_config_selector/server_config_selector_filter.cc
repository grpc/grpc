//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/server_config_selector/server_config_selector_filter.h"

#include "src/core/ext/filters/server_config_selector/server_config_selector.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

namespace {

class ChannelData {
 public:
  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector() {
    MutexLock lock(&mu_);
    return config_selector_.value();
  }

 private:
  class ServerConfigSelectorWatcher
      : public ServerConfigSelectorProvider::ServerConfigSelectorWatcher {
   public:
    explicit ServerConfigSelectorWatcher(ChannelData* chand) : chand_(chand) {}
    void OnServerConfigSelectorUpdate(
        absl::StatusOr<RefCountedPtr<ServerConfigSelector>> update) override {
      MutexLock lock(&chand_->mu_);
      chand_->config_selector_ = std::move(update);
    }

   private:
    ChannelData* chand_;
  };

  explicit ChannelData(RefCountedPtr<ServerConfigSelectorProvider>
                           server_config_selector_provider);
  ~ChannelData();

  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider_;
  Mutex mu_;
  absl::optional<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
      config_selector_ ABSL_GUARDED_BY(mu_);
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
  CallCombiner* call_combiner_;
  ServiceConfigCallData service_config_call_data_;
  // Overall error for the call
  grpc_error_handle error_ = GRPC_ERROR_NONE;
  // State for keeping track of recv_initial_metadata
  grpc_metadata_batch* recv_initial_metadata_ = nullptr;
  grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
  grpc_closure recv_initial_metadata_ready_;
  // State for keeping of track of recv_trailing_metadata
  grpc_closure* original_recv_trailing_metadata_ready_;
  grpc_closure recv_trailing_metadata_ready_;
  grpc_error_handle recv_trailing_metadata_ready_error_;
  bool seen_recv_trailing_metadata_ready_ = false;
};

// ChannelData

grpc_error_handle ChannelData::Init(grpc_channel_element* elem,
                                    grpc_channel_element_args* args) {
  GPR_ASSERT(elem->filter == &kServerConfigSelectorFilter);
  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider =
      ServerConfigSelectorProvider::GetFromChannelArgs(*args->channel_args);
  if (server_config_selector_provider == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No ServerConfigSelectorProvider object found");
  }
  new (elem->channel_data)
      ChannelData(std::move(server_config_selector_provider));
  return GRPC_ERROR_NONE;
}

void ChannelData::Destroy(grpc_channel_element* elem) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

ChannelData::ChannelData(
    RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider)
    : server_config_selector_provider_(
          std::move(server_config_selector_provider)) {
  GPR_ASSERT(server_config_selector_provider_ != nullptr);
  auto server_config_selector_watcher =
      absl::make_unique<ServerConfigSelectorWatcher>(this);
  auto config_selector = server_config_selector_provider_->Watch(
      std::move(server_config_selector_watcher));
  MutexLock lock(&mu_);
  // It's possible for the watcher to have already updated config_selector_
  if (!config_selector_.has_value()) {
    config_selector_ = std::move(config_selector);
  }
}

ChannelData::~ChannelData() { server_config_selector_provider_->CancelWatch(); }

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
    : call_context_(args.context), call_combiner_(args.call_combiner) {
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
}

CallData::~CallData() {
  // Remove the entry from call context, just in case anyone above us
  // tries to look at it during call stack destruction.
  call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value = nullptr;
  GRPC_ERROR_UNREF(error_);
}

void CallData::RecvInitialMetadataReady(void* user_data,
                                        grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (error == GRPC_ERROR_NONE) {
    auto config_selector = chand->config_selector();
    if (config_selector.ok()) {
      auto call_config =
          config_selector.value()->GetCallConfig(calld->recv_initial_metadata_);
      if (call_config.error != GRPC_ERROR_NONE) {
        calld->error_ = call_config.error;
        error = call_config.error;  // Does not take a ref
      } else {
        calld->service_config_call_data_ =
            ServiceConfigCallData(std::move(call_config.service_config),
                                  call_config.method_configs, {});
        calld->call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value =
            &calld->service_config_call_data_;
      }
    } else {
      calld->error_ = absl_status_to_grpc_error(config_selector.status());
      error = calld->error_;
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

const grpc_channel_filter kServerConfigSelectorFilter = {
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
    "server_config_selector_filter",
};

}  // namespace grpc_core
