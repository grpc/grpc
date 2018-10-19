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

#include <grpcpp/server_context.h>

#include <algorithm>
#include <mutex>
#include <utility>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/load_reporting.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/call.h>
#include <grpcpp/support/time.h>

#include "src/core/lib/surface/call.h"

namespace grpc {

// CompletionOp

class ServerContext::CompletionOp final : public internal::CallOpSetInterface {
 public:
  // initial refs: one in the server context, one in the cq
  CompletionOp()
      : has_tag_(false),
        tag_(nullptr),
        refs_(2),
        finalized_(false),
        cancelled_(0),
        done_intercepting_(false) {}

  void FillOps(internal::Call* call) override;
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

  /// TODO(vjpai): Allow override of cq_tag if appropriate for callback API
  void* cq_tag() override { return this; }

  void Unref();

  // This will be called while interceptors are run if the RPC is a hijacked
  // RPC. This should set hijacking state for each of the ops.
  void SetHijackingState() override {
    /* Servers don't allow hijacking */
    GPR_CODEGEN_ASSERT(false);
  }

  /* Should be called after interceptors are done running */
  void ContinueFillOpsAfterInterception() override {}

  /* Should be called after interceptors are done running on the finalize result
   * path */
  void ContinueFinalizeResultAfterInterception() override {
    done_intercepting_ = true;
    if (!has_tag_) {
      /* We don't have a tag to return. */
      std::unique_lock<std::mutex> lock(mu_);
      if (--refs_ == 0) {
        lock.unlock();
        delete this;
      }
      return;
    }
    /* Start a dummy op so that we can return the tag */
    GPR_CODEGEN_ASSERT(GRPC_CALL_OK ==
                       g_core_codegen_interface->grpc_call_start_batch(
                           call_.call(), nullptr, 0, this, nullptr));
  }

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
  bool done_intercepting_;
  internal::Call call_;
  internal::InterceptorBatchMethodsImpl interceptor_methods_;
};

void ServerContext::CompletionOp::Unref() {
  std::unique_lock<std::mutex> lock(mu_);
  if (--refs_ == 0) {
    lock.unlock();
    delete this;
  }
}

void ServerContext::CompletionOp::FillOps(internal::Call* call) {
  grpc_op ops;
  ops.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  ops.data.recv_close_on_server.cancelled = &cancelled_;
  ops.flags = 0;
  ops.reserved = nullptr;
  call_ = *call;
  interceptor_methods_.SetCall(&call_);
  interceptor_methods_.SetReverse();
  interceptor_methods_.SetCallOpSetInterface(this);
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_batch(call->call(), &ops, 1, this, nullptr));
  /* No interceptors to run here */
}

bool ServerContext::CompletionOp::FinalizeResult(void** tag, bool* status) {
  bool ret = false;
  std::unique_lock<std::mutex> lock(mu_);
  if (done_intercepting_) {
    /* We are done intercepting. */
    if (has_tag_) {
      *tag = tag_;
      ret = true;
    }
    if (--refs_ == 0) {
      lock.unlock();
      delete this;
    }
    return ret;
  }
  finalized_ = true;

  if (!*status) cancelled_ = 1;
  /* Release the lock since we are going to be running through interceptors now
   */
  lock.unlock();
  /* Add interception point and run through interceptors */
  interceptor_methods_.AddInterceptionHookPoint(
      experimental::InterceptionHookPoints::POST_RECV_CLOSE);
  if (interceptor_methods_.RunInterceptors()) {
    /* No interceptors were run */
    if (has_tag_) {
      *tag = tag_;
      ret = true;
    }
    lock.lock();
    if (--refs_ == 0) {
      lock.unlock();
      delete this;
    }
    return ret;
  }
  /* There are interceptors to be run. Return false for now */
  return false;
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
}

ServerContext::~ServerContext() {
  if (call_) {
    grpc_call_unref(call_);
  }
  if (completion_op_) {
    completion_op_->Unref();
  }
}

void ServerContext::BeginCompletionOp(internal::Call* call) {
  GPR_ASSERT(!completion_op_);
  completion_op_ = new CompletionOp();
  if (has_notify_when_done_tag_) {
    completion_op_->set_tag(async_notify_when_done_tag_);
  }
  call->PerformOps(completion_op_);
}

internal::CompletionQueueTag* ServerContext::GetCompletionOpTag() {
  return static_cast<internal::CompletionQueueTag*>(completion_op_);
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
      call_, GRPC_STATUS_CANCELLED, "Cancelled on the server side", nullptr);
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
  compression_algorithm_ = algorithm;
  const char* algorithm_name = nullptr;
  if (!grpc_compression_algorithm_name(algorithm, &algorithm_name)) {
    gpr_log(GPR_ERROR, "Name for compression algorithm '%d' unknown.",
            algorithm);
    abort();
  }
  GPR_ASSERT(algorithm_name != nullptr);
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
