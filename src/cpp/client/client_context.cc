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

#include <grpc/grpc.h>
#include <grpc/support/string_util.h>
#include <grpc++/credentials.h>
#include <grpc++/time.h>

#include "src/core/channel/compress_filter.h"
#include "src/cpp/common/create_auth_context.h"

namespace grpc {

ClientContext::ClientContext()
    : initial_metadata_received_(false),
      call_(nullptr),
      cq_(nullptr),
      deadline_(gpr_inf_future(GPR_CLOCK_REALTIME)) {}

ClientContext::~ClientContext() {
  if (call_) {
    grpc_call_destroy(call_);
  }
  if (cq_) {
    // Drain cq_.
    grpc_completion_queue_shutdown(cq_);
    while (grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME))
               .type != GRPC_QUEUE_SHUTDOWN)
      ;
    grpc_completion_queue_destroy(cq_);
  }
}

void ClientContext::AddMetadata(const grpc::string& meta_key,
                                const grpc::string& meta_value) {
  send_initial_metadata_.insert(std::make_pair(meta_key, meta_value));
}

void ClientContext::set_call(grpc_call* call,
                             const std::shared_ptr<ChannelInterface>& channel) {
  GPR_ASSERT(call_ == nullptr);
  call_ = call;
  channel_ = channel;
  if (creds_ && !creds_->ApplyToCall(call_)) {
    grpc_call_cancel_with_status(call, GRPC_STATUS_CANCELLED,
                                 "Failed to set credentials to rpc.");
  }
}

void ClientContext::_experimental_set_compression_algorithm(
    grpc_compression_algorithm algorithm) {
  char* algorithm_name = NULL;
  if (!grpc_compression_algorithm_name(algorithm, &algorithm_name)) {
    gpr_log(GPR_ERROR, "Name for compression algorithm '%d' unknown.",
            algorithm);
    abort();
  }
  GPR_ASSERT(algorithm_name != NULL);
  AddMetadata(GRPC_COMPRESS_REQUEST_ALGORITHM_KEY, algorithm_name);
}

std::shared_ptr<const AuthContext> ClientContext::auth_context() const {
  if (auth_context_.get() == nullptr) {
    auth_context_ = CreateAuthContext(call_);
  }
  return auth_context_;
}

void ClientContext::TryCancel() {
  if (call_) {
    grpc_call_cancel(call_);
  }
}

}  // namespace grpc
