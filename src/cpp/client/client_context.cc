//
//
// Copyright 2015 gRPC authors.
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

#include <stdlib.h>

#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/impl/compression_types.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/impl/interceptor_common.h>
#include <grpcpp/impl/sync.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/client_interceptor.h>

#include "src/core/lib/gprpp/crash.h"

namespace grpc {

class Channel;

class DefaultGlobalClientCallbacks final
    : public ClientContext::GlobalCallbacks {
 public:
  ~DefaultGlobalClientCallbacks() override {}
  void DefaultConstructor(ClientContext* /*context*/) override {}
  void Destructor(ClientContext* /*context*/) override {}
};

static DefaultGlobalClientCallbacks* g_default_client_callbacks =
    new DefaultGlobalClientCallbacks();
static ClientContext::GlobalCallbacks* g_client_callbacks =
    g_default_client_callbacks;

ClientContext::ClientContext()
    : initial_metadata_received_(false),
      wait_for_ready_(false),
      wait_for_ready_explicitly_set_(false),
      call_(nullptr),
      call_canceled_(false),
      deadline_(gpr_inf_future(GPR_CLOCK_REALTIME)),
      census_context_(nullptr),
      propagate_from_call_(nullptr),
      compression_algorithm_(GRPC_COMPRESS_NONE),
      initial_metadata_corked_(false) {
  g_client_callbacks->DefaultConstructor(this);
}

ClientContext::~ClientContext() {
  if (call_) {
    grpc_call_unref(call_);
    call_ = nullptr;
  }
  g_client_callbacks->Destructor(this);
}

void ClientContext::set_credentials(
    const std::shared_ptr<CallCredentials>& creds) {
  creds_ = creds;
  // If call_ is set, we have already created the call, and set the call
  // credentials. This should only be done before we have started the batch
  // for sending initial metadata.
  if (creds_ != nullptr && call_ != nullptr) {
    if (!creds_->ApplyToCall(call_)) {
      SendCancelToInterceptors();
      grpc_call_cancel_with_status(call_, GRPC_STATUS_CANCELLED,
                                   "Failed to set credentials to rpc.",
                                   nullptr);
    }
  }
}

std::unique_ptr<ClientContext> ClientContext::FromInternalServerContext(
    const grpc::ServerContextBase& context, PropagationOptions options) {
  std::unique_ptr<ClientContext> ctx(new ClientContext);
  ctx->propagate_from_call_ = context.call_.call;
  ctx->propagation_options_ = options;
  return ctx;
}

std::unique_ptr<ClientContext> ClientContext::FromServerContext(
    const grpc::ServerContextBase& server_context, PropagationOptions options) {
  return FromInternalServerContext(server_context, options);
}

std::unique_ptr<ClientContext> ClientContext::FromCallbackServerContext(
    const grpc::CallbackServerContext& server_context,
    PropagationOptions options) {
  return FromInternalServerContext(server_context, options);
}

void ClientContext::AddMetadata(const std::string& meta_key,
                                const std::string& meta_value) {
  send_initial_metadata_.insert(std::make_pair(meta_key, meta_value));
}

void ClientContext::set_call(grpc_call* call,
                             const std::shared_ptr<Channel>& channel) {
  internal::MutexLock lock(&mu_);
  GPR_ASSERT(call_ == nullptr);
  call_ = call;
  channel_ = channel;
  if (creds_ && !creds_->ApplyToCall(call_)) {
    // TODO(yashykt): should interceptors also see this status?
    SendCancelToInterceptors();
    grpc_call_cancel_with_status(call, GRPC_STATUS_CANCELLED,
                                 "Failed to set credentials to rpc.", nullptr);
  }
  if (call_canceled_) {
    SendCancelToInterceptors();
    grpc_call_cancel(call_, nullptr);
  }
}

void ClientContext::set_compression_algorithm(
    grpc_compression_algorithm algorithm) {
  compression_algorithm_ = algorithm;
  const char* algorithm_name = nullptr;
  if (!grpc_compression_algorithm_name(algorithm, &algorithm_name)) {
    grpc_core::Crash(absl::StrFormat(
        "Name for compression algorithm '%d' unknown.", algorithm));
  }
  GPR_ASSERT(algorithm_name != nullptr);
  AddMetadata(GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY, algorithm_name);
}

void ClientContext::TryCancel() {
  internal::MutexLock lock(&mu_);
  if (call_) {
    SendCancelToInterceptors();
    grpc_call_cancel(call_, nullptr);
  } else {
    call_canceled_ = true;
  }
}

void ClientContext::SendCancelToInterceptors() {
  internal::CancelInterceptorBatchMethods cancel_methods;
  for (size_t i = 0; i < rpc_info_.interceptors_.size(); i++) {
    rpc_info_.RunInterceptor(&cancel_methods, i);
  }
}

std::string ClientContext::peer() const {
  std::string peer;
  if (call_) {
    char* c_peer = grpc_call_get_peer(call_);
    peer = c_peer;
    gpr_free(c_peer);
  }
  return peer;
}

void ClientContext::SetGlobalCallbacks(GlobalCallbacks* client_callbacks) {
  GPR_ASSERT(g_client_callbacks == g_default_client_callbacks);
  GPR_ASSERT(client_callbacks != nullptr);
  GPR_ASSERT(client_callbacks != g_default_client_callbacks);
  g_client_callbacks = client_callbacks;
}

}  // namespace grpc
