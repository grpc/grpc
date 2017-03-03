/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/client_context.h>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

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
    grpc_call_destroy(call_);
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
  char* algorithm_name = nullptr;
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
  GPR_ASSERT(client_callbacks != NULL);
  GPR_ASSERT(client_callbacks != &g_default_client_callbacks);
  g_client_callbacks = client_callbacks;
}

}  // namespace grpc
