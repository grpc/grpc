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

#include <grpc++/server_context.h>

#include <mutex>

#include <grpc++/impl/call.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "src/cpp/util/time.h"

namespace grpc {

// CompletionOp

class ServerContext::CompletionOp GRPC_FINAL : public CallOpBuffer {
 public:
  // initial refs: one in the server context, one in the cq
  CompletionOp() : refs_(2), finalized_(false), cancelled_(false) {
    AddServerRecvClose(&cancelled_);
  }
  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE;

  bool CheckCancelled(CompletionQueue* cq);

  void Unref();

 private:
  std::mutex mu_;
  int refs_;
  bool finalized_;
  bool cancelled_;
};

void ServerContext::CompletionOp::Unref() {
  std::unique_lock<std::mutex> lock(mu_);
  if (--refs_ == 0) {
    lock.unlock();
    delete this;
  }
}

bool ServerContext::CompletionOp::CheckCancelled(CompletionQueue* cq) {
  cq->TryPluck(this);
  std::lock_guard<std::mutex> g(mu_);
  return finalized_ ? cancelled_ : false;
}

bool ServerContext::CompletionOp::FinalizeResult(void** tag, bool* status) {
  GPR_ASSERT(CallOpBuffer::FinalizeResult(tag, status));
  std::unique_lock<std::mutex> lock(mu_);
  finalized_ = true;
  if (!*status) cancelled_ = true;
  if (--refs_ == 0) {
    lock.unlock();
    delete this;
  }
  return false;
}

// ServerContext body

ServerContext::ServerContext()
    : completion_op_(nullptr),
      call_(nullptr),
      cq_(nullptr),
      sent_initial_metadata_(false) {}

ServerContext::ServerContext(gpr_timespec deadline, grpc_metadata* metadata,
                             size_t metadata_count)
    : completion_op_(nullptr),
      deadline_(Timespec2Timepoint(deadline)),
      call_(nullptr),
      cq_(nullptr),
      sent_initial_metadata_(false) {
  for (size_t i = 0; i < metadata_count; i++) {
    client_metadata_.insert(std::make_pair(
        grpc::string(metadata[i].key),
        grpc::string(metadata[i].value,
                     metadata[i].value + metadata[i].value_length)));
  }
}

ServerContext::~ServerContext() {
  if (call_) {
    grpc_call_destroy(call_);
  }
  if (completion_op_) {
    completion_op_->Unref();
  }
}

void ServerContext::BeginCompletionOp(Call* call) {
  GPR_ASSERT(!completion_op_);
  completion_op_ = new CompletionOp();
  call->PerformOps(completion_op_);
}

void ServerContext::AddInitialMetadata(const grpc::string& key,
                                       const grpc::string& value) {
  initial_metadata_.insert(std::make_pair(key, value));
}

void ServerContext::AddTrailingMetadata(const grpc::string& key,
                                        const grpc::string& value) {
  trailing_metadata_.insert(std::make_pair(key, value));
}

bool ServerContext::IsCancelled() {
  return completion_op_ && completion_op_->CheckCancelled(cq_);
}

}  // namespace grpc
