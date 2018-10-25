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
  // must ref the call before calling constructor and after deleting this
  CompletionOp(grpc_call* call)
      : call_(call),
        has_tag_(false),
        tag_(nullptr),
        refs_(2),
        finalized_(false),
        cancelled_(0) {}

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

  /// TODO(vjpai): Allow override of cq_tag if appropriate for callback API
  void* cq_tag() override { return this; }

  void Unref();

 private:
  bool CheckCancelledNoPluck() {
    std::lock_guard<std::mutex> g(mu_);
    return finalized_ ? (cancelled_ != 0) : false;
  }

  grpc_call* call_;
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
    // Save aside the call pointer before deleting for later unref
    grpc_call* call = call_;
    delete this;
    grpc_call_unref(call);
  }
}

void ServerContext::CompletionOp::FillOps(grpc_call* call, grpc_op* ops,
                                          size_t* nops) {
  ops->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  ops->data.recv_close_on_server.cancelled = &cancelled_;
  ops->flags = 0;
  ops->reserved = nullptr;
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
    // Save aside the call pointer before deleting for later unref
    grpc_call* call = call_;
    delete this;
    grpc_call_unref(call);
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
  grpc_call_ref(call->call());
  completion_op_ =
      new (grpc_call_arena_alloc(call->call(), sizeof(CompletionOp)))
          CompletionOp(call->call());
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
