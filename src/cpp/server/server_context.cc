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

  void FillOps(grpc_call* call, grpc_op* ops, size_t* nops) override;
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

void ServerContext::CompletionOp::FillOps(grpc_call* call, grpc_op* ops,
                                          size_t* nops) {
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
      compression_level_set_(false),
      has_pending_ops_(false) {}

ServerContext::ServerContext(gpr_timespec deadline, grpc_metadata_array* arr)
    : completion_op_(nullptr),
      has_notify_when_done_tag_(false),
      async_notify_when_done_tag_(nullptr),
      deadline_(deadline),
      call_(nullptr),
      cq_(nullptr),
      sent_initial_metadata_(false),
      compression_level_set_(false),
      has_pending_ops_(false) {
  std::swap(*client_metadata_.arr(), *arr);
  client_metadata_.FillMap();
}

ServerContext::~ServerContext() {
  if (call_) {
    grpc_call_unref(call_);
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

CompletionQueueTag* ServerContext::GetCompletionOpTag() {
  return static_cast<CompletionQueueTag*>(completion_op_);
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
  for (const auto& cost_datum : cost_data) {
    AddTrailingMetadata(GRPC_LB_COST_MD_KEY, cost_datum);
  }
}

}  // namespace grpc
