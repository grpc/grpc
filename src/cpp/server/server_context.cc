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

#include <algorithm>
#include <mutex>
#include <utility>

#include <grpc++/completion_queue.h>
#include <grpc++/impl/call.h>
#include <grpc++/support/time.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/load_reporting.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/surface/call.h"

namespace grpc {

// CompletionOp

class ServerContext::CompletionOp final : public CallOpSetInterface {
 public:
  // initial refs: one in the server context, one in the cq
  CompletionOp()
      : has_tag_(false),
        tag_(nullptr),
        refs_(2),
        finalized_(false),
        cancelled_(0) {}

  void FillOps(grpc_op* ops, size_t* nops) override;
  bool FinalizeResult(void** tag, bool* status) override;

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
    std::lock_guard<std::mutex> g(mu_);
    return finalized_ ? (cancelled_ != 0) : false;
  }

  bool has_tag_;
  void* tag_;
  std::mutex mu_;
  int refs_;
  bool finalized_;
  int cancelled_;
};

void ServerContext::CompletionOp::Unref() {
  std::unique_lock<std::mutex> lock(mu_);
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
  std::unique_lock<std::mutex> lock(mu_);
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
      sent_initial_metadata_(false),
      compression_level_set_(false) {}

ServerContext::ServerContext(gpr_timespec deadline, grpc_metadata_array* arr)
    : completion_op_(nullptr),
      has_notify_when_done_tag_(false),
      async_notify_when_done_tag_(nullptr),
      deadline_(deadline),
      call_(nullptr),
      cq_(nullptr),
      sent_initial_metadata_(false),
      compression_level_set_(false) {
  std::swap(*client_metadata_.arr(), *arr);
  client_metadata_.FillMap();
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

void ServerContext::SetLoadReportingCosts(
    const std::vector<grpc::string>& cost_data) {
  if (call_ == nullptr) return;
  grpc_load_reporting_cost_context* cost_ctx =
      static_cast<grpc_load_reporting_cost_context*>(
          gpr_malloc(sizeof(*cost_ctx)));
  cost_ctx->values_count = cost_data.size();
  cost_ctx->values = static_cast<grpc_slice*>(
      gpr_malloc(sizeof(*cost_ctx->values) * cost_ctx->values_count));
  for (size_t i = 0; i < cost_ctx->values_count; ++i) {
    cost_ctx->values[i] =
        grpc_slice_from_copied_buffer(cost_data[i].data(), cost_data[i].size());
  }
  grpc_call_set_load_reporting_cost_context(call_, cost_ctx);
}

}  // namespace grpc
