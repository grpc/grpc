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
#include <grpcpp/support/server_callback.h>

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

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/surface/call.h"

namespace grpc {

// CompletionOp

class ServerContext::CompletionOp final : public internal::CallOpSetInterface {
 public:
  // initial refs: one in the server context, one in the cq
  // must ref the call before calling constructor and after deleting this
  CompletionOp(internal::Call* call, internal::ServerReactor* reactor)
      : call_(*call),
        reactor_(reactor),
        has_tag_(false),
        tag_(nullptr),
        core_cq_tag_(this),
        refs_(2),
        finalized_(false),
        cancelled_(0),
        done_intercepting_(false) {}

  // CompletionOp isn't copyable or movable
  CompletionOp(const CompletionOp&) = delete;
  CompletionOp& operator=(const CompletionOp&) = delete;
  CompletionOp(CompletionOp&&) = delete;
  CompletionOp& operator=(CompletionOp&&) = delete;

  ~CompletionOp() {
    if (call_.server_rpc_info()) {
      call_.server_rpc_info()->Unref();
    }
  }

  void FillOps(internal::Call* call) override;

  // This should always be arena allocated in the call, so override delete.
  // But this class is not trivially destructible, so must actually call delete
  // before allowing the arena to be freed
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(CompletionOp));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

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

  void SetCancelCallback(std::function<void()> callback) {
    grpc_core::MutexLock lock(&mu_);

    if (finalized_ && (cancelled_ != 0)) {
      callback();
      return;
    }

    cancel_callback_ = std::move(callback);
  }

  void ClearCancelCallback() {
    grpc_core::MutexLock g(&mu_);
    cancel_callback_ = nullptr;
  }

  void set_core_cq_tag(void* core_cq_tag) { core_cq_tag_ = core_cq_tag; }

  void* core_cq_tag() override { return core_cq_tag_; }

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
      Unref();
      return;
    }
    /* Start a dummy op so that we can return the tag */
    GPR_CODEGEN_ASSERT(
        GRPC_CALL_OK ==
        grpc_call_start_batch(call_.call(), nullptr, 0, core_cq_tag_, nullptr));
  }

 private:
  bool CheckCancelledNoPluck() {
    grpc_core::MutexLock lock(&mu_);
    return finalized_ ? (cancelled_ != 0) : false;
  }

  internal::Call call_;
  internal::ServerReactor* const reactor_;
  bool has_tag_;
  void* tag_;
  void* core_cq_tag_;
  grpc_core::RefCount refs_;
  grpc_core::Mutex mu_;
  bool finalized_;
  int cancelled_;  // This is an int (not bool) because it is passed to core
  std::function<void()> cancel_callback_;
  bool done_intercepting_;
  internal::InterceptorBatchMethodsImpl interceptor_methods_;
};

void ServerContext::CompletionOp::Unref() {
  if (refs_.Unref()) {
    grpc_call* call = call_.call();
    delete this;
    grpc_call_unref(call);
  }
}

void ServerContext::CompletionOp::FillOps(internal::Call* call) {
  grpc_op ops;
  ops.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  ops.data.recv_close_on_server.cancelled = &cancelled_;
  ops.flags = 0;
  ops.reserved = nullptr;
  interceptor_methods_.SetCall(&call_);
  interceptor_methods_.SetReverse();
  interceptor_methods_.SetCallOpSetInterface(this);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call->call(), &ops, 1,
                                                   core_cq_tag_, nullptr));
  /* No interceptors to run here */
}

bool ServerContext::CompletionOp::FinalizeResult(void** tag, bool* status) {
  bool ret = false;
  grpc_core::ReleasableMutexLock lock(&mu_);
  if (done_intercepting_) {
    /* We are done intercepting. */
    if (has_tag_) {
      *tag = tag_;
      ret = true;
    }
    Unref();
    return ret;
  }
  finalized_ = true;

  // If for some reason the incoming status is false, mark that as a
  // cancellation.
  // TODO(vjpai): does this ever happen?
  if (!*status) {
    cancelled_ = 1;
  }

  // Decide whether to call the cancel callback before releasing the lock
  bool call_cancel = (cancelled_ != 0);

  // If it's a unary cancel callback, call it under the lock so that it doesn't
  // race with ClearCancelCallback. Although we don't normally call callbacks
  // under a lock, this is a special case since the user needs a guarantee that
  // the callback won't issue or run after ClearCancelCallback has returned.
  // This requirement imposes certain restrictions on the callback, documented
  // in the API comments of SetCancelCallback.
  if (cancel_callback_) {
    cancel_callback_();
  }

  // Release the lock since we may call a callback and interceptors now.
  lock.Unlock();

  if (call_cancel && reactor_ != nullptr) {
    reactor_->MaybeCallOnCancel();
  }
  /* Add interception point and run through interceptors */
  interceptor_methods_.AddInterceptionHookPoint(
      experimental::InterceptionHookPoints::POST_RECV_CLOSE);
  if (interceptor_methods_.RunInterceptors()) {
    /* No interceptors were run */
    if (has_tag_) {
      *tag = tag_;
      ret = true;
    }
    Unref();
    return ret;
  }
  /* There are interceptors to be run. Return false for now */
  return false;
}

// ServerContext body

ServerContext::ServerContext() { Setup(gpr_inf_future(GPR_CLOCK_REALTIME)); }

ServerContext::ServerContext(gpr_timespec deadline, grpc_metadata_array* arr) {
  Setup(deadline);
  std::swap(*client_metadata_.arr(), *arr);
}

void ServerContext::Setup(gpr_timespec deadline) {
  completion_op_ = nullptr;
  has_notify_when_done_tag_ = false;
  async_notify_when_done_tag_ = nullptr;
  deadline_ = deadline;
  call_ = nullptr;
  cq_ = nullptr;
  sent_initial_metadata_ = false;
  compression_level_set_ = false;
  has_pending_ops_ = false;
  rpc_info_ = nullptr;
}

void ServerContext::BindDeadlineAndMetadata(gpr_timespec deadline,
                                            grpc_metadata_array* arr) {
  deadline_ = deadline;
  std::swap(*client_metadata_.arr(), *arr);
}

ServerContext::~ServerContext() { Clear(); }

void ServerContext::Clear() {
  auth_context_.reset();
  initial_metadata_.clear();
  trailing_metadata_.clear();
  client_metadata_.Reset();
  if (completion_op_) {
    completion_op_->Unref();
    completion_op_ = nullptr;
    completion_tag_.Clear();
  }
  if (rpc_info_) {
    rpc_info_->Unref();
    rpc_info_ = nullptr;
  }
  if (call_) {
    auto* call = call_;
    call_ = nullptr;
    grpc_call_unref(call);
  }
}

void ServerContext::BeginCompletionOp(internal::Call* call,
                                      std::function<void(bool)> callback,
                                      internal::ServerReactor* reactor) {
  GPR_ASSERT(!completion_op_);
  if (rpc_info_) {
    rpc_info_->Ref();
  }
  grpc_call_ref(call->call());
  completion_op_ =
      new (grpc_call_arena_alloc(call->call(), sizeof(CompletionOp)))
          CompletionOp(call, reactor);
  if (callback != nullptr) {
    completion_tag_.Set(call->call(), std::move(callback), completion_op_);
    completion_op_->set_core_cq_tag(&completion_tag_);
    completion_op_->set_tag(completion_op_);
  } else if (has_notify_when_done_tag_) {
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
  internal::CancelInterceptorBatchMethods cancel_methods;
  if (rpc_info_) {
    for (size_t i = 0; i < rpc_info_->interceptors_.size(); i++) {
      rpc_info_->RunInterceptor(&cancel_methods, i);
    }
  }
  grpc_call_error err = grpc_call_cancel_with_status(
      call_, GRPC_STATUS_CANCELLED, "Cancelled on the server side", nullptr);
  if (err != GRPC_CALL_OK) {
    gpr_log(GPR_ERROR, "TryCancel failed with: %d", err);
  }
}

void ServerContext::SetCancelCallback(std::function<void()> callback) {
  completion_op_->SetCancelCallback(std::move(callback));
}

void ServerContext::ClearCancelCallback() {
  completion_op_->ClearCancelCallback();
}

bool ServerContext::IsCancelled() const {
  if (completion_tag_) {
    // When using callback API, this result is always valid.
    return completion_op_->CheckCancelledAsync();
  } else if (has_notify_when_done_tag_) {
    // When using async API, the result is only valid
    // if the tag has already been delivered at the completion queue
    return completion_op_ && completion_op_->CheckCancelledAsync();
  } else {
    // when using sync API, the result is always valid
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
