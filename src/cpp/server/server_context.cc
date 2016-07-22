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

#include <grpc++/completion_queue.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/sync.h>
#include <grpc++/support/time.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/surface/call.h"

namespace grpc {

// CompletionOp

class ServerContext::CompletionOp GRPC_FINAL : public CallOpSetInterface {
 public:
  // initial refs: one in the server context, one in the cq
  CompletionOp()
      : has_tag_(false),
        tag_(nullptr),
        refs_(2),
        finalized_(false),
        cancelled_(0) {}

  void FillOps(grpc_op* ops, size_t* nops) GRPC_OVERRIDE;
  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE;

  bool CheckCancelled(CompletionQueue* cq) {
    cq->TryPluck(this);
    return CheckCancelledNoPluck();
  }
  bool CheckCancelledAsync() { return CheckCancelledNoPluck(); }

  void set_tag(void* tag) {
    has_tag_ = true;
    tag_ = tag;
  }

  void Unref();

 private:
  bool CheckCancelledNoPluck() {
    grpc::lock_guard<grpc::mutex> g(mu_);
    return finalized_ ? (cancelled_ != 0) : false;
  }

  bool has_tag_;
  void* tag_;
  grpc::mutex mu_;
  int refs_;
  bool finalized_;
  int cancelled_;
};

void ServerContext::CompletionOp::Unref() {
  grpc::unique_lock<grpc::mutex> lock(mu_);
  if (--refs_ == 0) {
    lock.unlock();
    delete this;
  }
}

void ServerContext::CompletionOp::FillOps(grpc_op* ops, size_t* nops) {
  ops->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  ops->data.recv_close_on_server.cancelled = &cancelled_;
  ops->flags = 0;
  ops->reserved = NULL;
  *nops = 1;
}

bool ServerContext::CompletionOp::FinalizeResult(void** tag, bool* status) {
  grpc::unique_lock<grpc::mutex> lock(mu_);
  finalized_ = true;
  bool ret = false;
  if (has_tag_) {
    *tag = tag_;
    ret = true;
  }
  if (!*status) cancelled_ = 1;
  if (--refs_ == 0) {
    lock.unlock();
    delete this;
  }
  return ret;
}

// ServerContext body

ServerContext::ServerContext()
    : completion_op_(nullptr),
      has_notify_when_done_tag_(false),
      async_notify_when_done_tag_(nullptr),
      deadline_(gpr_inf_future(GPR_CLOCK_REALTIME)),
      call_(nullptr),
      cq_(nullptr),
      sent_initial_metadata_(false) {}

ServerContext::ServerContext(gpr_timespec deadline, grpc_metadata* metadata,
                             size_t metadata_count)
    : completion_op_(nullptr),
      has_notify_when_done_tag_(false),
      async_notify_when_done_tag_(nullptr),
      deadline_(deadline),
      call_(nullptr),
      cq_(nullptr),
      sent_initial_metadata_(false) {
  for (size_t i = 0; i < metadata_count; i++) {
    client_metadata_.insert(std::pair<grpc::string_ref, grpc::string_ref>(
        metadata[i].key,
        grpc::string_ref(metadata[i].value, metadata[i].value_length)));
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
  if (has_notify_when_done_tag_) {
    completion_op_->set_tag(async_notify_when_done_tag_);
  }
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

void ServerContext::TryCancel() const {
  grpc_call_error err = grpc_call_cancel_with_status(
      call_, GRPC_STATUS_CANCELLED, "Cancelled on the server side", NULL);
  if (err != GRPC_CALL_OK) {
    gpr_log(GPR_ERROR, "TryCancel failed with: %d", err);
  }
}

bool ServerContext::IsCancelled() const {
  if (has_notify_when_done_tag_) {
    // when using async API, but the result is only valid
    // if the tag has already been delivered at the completion queue
    return completion_op_ && completion_op_->CheckCancelledAsync();
  } else {
    // when using sync API
    return completion_op_ && completion_op_->CheckCancelled(cq_);
  }
}

void ServerContext::set_compression_level(grpc_compression_level level) {
  // TODO(dgq): get rid of grpc_call_compression_for_level and propagate the
  // compression level by adding a new argument to
  // CallOpSendInitialMetadata::SendInitialMetadata.
  const grpc_compression_algorithm algorithm_for_level =
      grpc_call_compression_for_level(call_, level);
  set_compression_algorithm(algorithm_for_level);
}

void ServerContext::set_compression_algorithm(
    grpc_compression_algorithm algorithm) {
  char* algorithm_name = NULL;
  if (!grpc_compression_algorithm_name(algorithm, &algorithm_name)) {
    gpr_log(GPR_ERROR, "Name for compression algorithm '%d' unknown.",
            algorithm);
    abort();
  }
  GPR_ASSERT(algorithm_name != NULL);
  AddInitialMetadata(GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY, algorithm_name);
}

grpc::string ServerContext::peer() const {
  grpc::string peer;
  if (call_) {
    char* c_peer = grpc_call_get_peer(call_);
    peer = c_peer;
    gpr_free(c_peer);
  }
  return peer;
}

const struct census_context* ServerContext::census_context() const {
  return grpc_census_call_get_context(call_);
}

}  // namespace grpc
