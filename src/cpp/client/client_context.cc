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

#include <grpc++/client_context.h>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include <grpc++/impl/grpc_library.h>
#include <grpc++/security/credentials.h>
#include <grpc++/server_context.h>
#include <grpc++/support/time.h>

namespace grpc {

class DefaultGlobalClientCallbacks final
    : public ClientContext::GlobalCallbacks {
 public:
  ~DefaultGlobalClientCallbacks() override {}
  void DefaultConstructor(ClientContext* context) override {}
  void Destructor(ClientContext* context) override {}
};

static internal::GrpcLibraryInitializer g_gli_initializer;
static DefaultGlobalClientCallbacks g_default_client_callbacks;
static ClientContext::GlobalCallbacks* g_client_callbacks =
    &g_default_client_callbacks;

ClientContext::ClientContext()
    : initial_metadata_received_(false),
      wait_for_ready_(false),
      wait_for_ready_explicitly_set_(false),
      idempotent_(false),
      cacheable_(false),
      call_(nullptr),
      call_canceled_(false),
      deadline_(gpr_inf_future(GPR_CLOCK_REALTIME)),
      census_context_(nullptr),
      propagate_from_call_(nullptr),
      initial_metadata_corked_(false) {
  g_client_callbacks->DefaultConstructor(this);
}

ClientContext::~ClientContext() {
  if (call_) {
    grpc_call_unref(call_);
  }
  g_client_callbacks->Destructor(this);
}

std::unique_ptr<ClientContext> ClientContext::FromServerContext(
    const ServerContext& context, PropagationOptions options) {
  std::unique_ptr<ClientContext> ctx(new ClientContext);
  ctx->propagate_from_call_ = context.call_;
  ctx->propagation_options_ = options;
  return ctx;
}

void ClientContext::AddMetadata(const grpc::string& meta_key,
                                const grpc::string& meta_value) {
  send_initial_metadata_.insert(std::make_pair(meta_key, meta_value));
}

void ClientContext::set_call(grpc_call* call,
                             const std::shared_ptr<Channel>& channel) {
  std::unique_lock<std::mutex> lock(mu_);
  GPR_ASSERT(call_ == nullptr);
  call_ = call;
  channel_ = channel;
  if (creds_ && !creds_->ApplyToCall(call_)) {
    grpc_call_cancel_with_status(call, GRPC_STATUS_CANCELLED,
                                 "Failed to set credentials to rpc.", nullptr);
  }
  if (call_canceled_) {
    grpc_call_cancel(call_, nullptr);
  }
}

void ClientContext::set_compression_algorithm(
    grpc_compression_algorithm algorithm) {
  const char* algorithm_name = nullptr;
  if (!grpc_compression_algorithm_name(algorithm, &algorithm_name)) {
    gpr_log(GPR_ERROR, "Name for compression algorithm '%d' unknown.",
            algorithm);
    abort();
  }
  GPR_ASSERT(algorithm_name != nullptr);
  AddMetadata(GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY, algorithm_name);
}

void ClientContext::TryCancel() {
  std::unique_lock<std::mutex> lock(mu_);
  if (call_) {
    grpc_call_cancel(call_, nullptr);
  } else {
    call_canceled_ = true;
  }
}

grpc::string ClientContext::peer() const {
  grpc::string peer;
  if (call_) {
    char* c_peer = grpc_call_get_peer(call_);
    peer = c_peer;
    gpr_free(c_peer);
  }
  return peer;
}

void ClientContext::SetGlobalCallbacks(GlobalCallbacks* client_callbacks) {
  GPR_ASSERT(g_client_callbacks == &g_default_client_callbacks);
  GPR_ASSERT(client_callbacks != nullptr);
  GPR_ASSERT(client_callbacks != &g_default_client_callbacks);
  g_client_callbacks = client_callbacks;
}

}  // namespace grpc
