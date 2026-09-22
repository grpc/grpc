//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <assert.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/impl/compression_types.h>
#include <grpc/load_reporting.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/ext/server_metric_recorder.h>
#include <grpcpp/impl/call.h>
#include <grpcpp/impl/call_op_set.h>
#include <grpcpp/impl/call_op_set_interface.h>
#include <grpcpp/impl/completion_queue_tag.h>
#include <grpcpp/impl/interceptor_common.h>
#include <grpcpp/impl/metadata_map.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/callback_common.h>
#include <grpcpp/support/interceptor.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/server_interceptor.h>
#include <grpcpp/support/string_ref.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/surface/call.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/sync.h"
#include "src/cpp/server/backend_metric_recorder.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace grpc {
namespace internal {

class SessionContextRegistry {
 public:
  static uint16_t Register(void (*destroy)(void*)) {
    auto* state = GetState();
    grpc_core::MutexLock lock(&state->mu);
    uint16_t id = state->destroy_functions.size();
    GRPC_CHECK_LT(id, std::numeric_limits<uint16_t>::max());
    state->destroy_functions.push_back(destroy);
    return id;
  }

  static void DestroyElement(uint16_t id, void* element) {
    if (element == nullptr) return;
    auto* state = GetState();
    void (*destroy)(void*) = nullptr;
    {
      grpc_core::MutexLock lock(&state->mu);
      if (id < state->destroy_functions.size()) {
        destroy = state->destroy_functions[id];
      }
    }
    if (destroy != nullptr) {
      destroy(element);
    }
  }

 private:
  struct RegistryState {
    grpc_core::Mutex mu;
    std::vector<void (*)(void*)> destroy_functions;
  };

  static RegistryState* GetState() {
    static grpc_core::NoDestruct<RegistryState> state;
    return state.get();
  }
};

class SessionContextList {
 public:
  ~SessionContextList() {
    for (uint16_t i = 0; i < num_elements_; ++i) {
      if (elements_[i] != nullptr) {
        SessionContextRegistry::DestroyElement(i, elements_[i]);
      }
    }
    delete[] elements_;
  }

  void Set(uint16_t id, void* ptr) {
    // Resize the array to fit `id`. Since Set() is called only during the
    // session handshake and the number of attached contexts is expected to be
    // extremely small (typically <= 3), a simple, minimal array resizing
    // allocation is sufficient here.
    if (id >= num_elements_) {
      uint16_t new_num = id + 1;
      void** new_elements = new void*[new_num]();
      for (uint16_t i = 0; i < num_elements_; ++i) {
        new_elements[i] = elements_[i];
      }
      delete[] elements_;
      elements_ = new_elements;
      num_elements_ = new_num;
    }
    if (elements_[id] != nullptr) {
      SessionContextRegistry::DestroyElement(id, elements_[id]);
    }
    elements_[id] = ptr;
  }

  void* Get(uint16_t id) const {
    if (id < num_elements_ && elements_[id] != nullptr) {
      return elements_[id];
    }
    return nullptr;
  }

 private:
  void** elements_ = nullptr;
  uint16_t num_elements_ = 0;
};

uint16_t ServerContextRegisterSessionContext(void (*destroy)(void*)) {
  return SessionContextRegistry::Register(destroy);
}

void ServerContextSetSessionContext(grpc_call* call, uint16_t id, void* ptr) {
  if (call == nullptr || id == std::numeric_limits<uint16_t>::max()) {
    if (ptr != nullptr) {
      SessionContextRegistry::DestroyElement(id, ptr);
    }
    return;
  }
  grpc_core::Arena* arena = grpc_call_get_arena(call);
  auto* list = arena->GetContext<SessionContextList>();
  if (list == nullptr) {
    list = arena->New<SessionContextList>();
    arena->SetContext<SessionContextList>(list);
  }
  list->Set(id, ptr);
}

void* ServerContextGetSessionContext(grpc_call* call, uint16_t id) {
  if (call == nullptr || id == std::numeric_limits<uint16_t>::max()) {
    return nullptr;
  }
  grpc_core::Arena* arena = grpc_call_get_arena(call);

  auto* list = arena->GetContext<SessionContextList>();
  if (list != nullptr) {
    void* ptr = list->Get(id);
    if (ptr != nullptr) return ptr;
  }

  auto* parent_call_context = arena->GetContext<grpc_core::ParentCallContext>();
  if (parent_call_context != nullptr && parent_call_context->arena != nullptr) {
    auto* parent_list =
        parent_call_context->arena->GetContext<SessionContextList>();
    if (parent_list != nullptr) {
      return parent_list->Get(id);
    }
  }

  return nullptr;
}

}  // namespace internal
}  // namespace grpc

namespace grpc_core {
template <>
struct ArenaContextType<grpc::internal::SessionContextList> {
  static void Destroy(grpc::internal::SessionContextList* p) {
    p->~SessionContextList();
  }
};
}  // namespace grpc_core

namespace grpc {

// CompletionOp

class ServerContextBase::CompletionOp final
    : public internal::CallOpSetInterface {
 public:
  // initial refs: one in the server context, one in the cq
  // must ref the call before calling constructor and after deleting this
  CompletionOp(internal::Call* call,
               grpc::internal::ServerCallbackCall* callback_controller)
      : call_(*call),
        callback_controller_(callback_controller),
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

  ~CompletionOp() override {
    if (call_.server_rpc_info()) {
      call_.server_rpc_info()->Unref();
    }
  }

  void FillOps(internal::Call* call) override;

  // This should always be arena allocated in the call, so override delete.
  // But this class is not trivially destructible, so must actually call delete
  // before allowing the arena to be freed
  static void operator delete(void* /*ptr*/, std::size_t size) {
    // Use size to avoid unused-parameter warning since assert seems to be
    // compiled out and treated as unused in some gcc optimized versions.
    (void)size;
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

  void set_core_cq_tag(void* core_cq_tag) { core_cq_tag_ = core_cq_tag; }

  void* core_cq_tag() override { return core_cq_tag_; }

  void Unref();

  // This will be called while interceptors are run if the RPC is a hijacked
  // RPC. This should set hijacking state for each of the ops.
  void SetHijackingState() override {
    // Servers don't allow hijacking
    grpc_core::Crash("unreachable");
  }

  // Should be called after interceptors are done running
  void ContinueFillOpsAfterInterception() override {}

  // Should be called after interceptors are done running on the finalize result
  // path
  void ContinueFinalizeResultAfterInterception() override {
    done_intercepting_ = true;
    if (!has_tag_) {
      // We don't have a tag to return.
      Unref();
      // Unref can delete this, so do not access anything from this afterward.
      return;
    }
    // Start a phony op so that we can return the tag
    GRPC_CHECK(grpc_call_start_batch(call_.call(), nullptr, 0, core_cq_tag_,
                                     nullptr) == GRPC_CALL_OK);
  }

 private:
  bool CheckCancelledNoPluck() {
    grpc_core::MutexLock lock(&mu_);
    return finalized_ ? (cancelled_ != 0) : false;
  }

  internal::Call call_;
  grpc::internal::ServerCallbackCall* const callback_controller_;
  bool has_tag_;
  void* tag_;
  void* core_cq_tag_;
  grpc_core::RefCount refs_;
  grpc_core::Mutex mu_;
  bool finalized_;
  int cancelled_;  // This is an int (not bool) because it is passed to core
  bool done_intercepting_;
  internal::InterceptorBatchMethodsImpl interceptor_methods_;
};

void ServerContextBase::CompletionOp::Unref() {
  if (refs_.Unref()) {
    grpc_call* call = call_.call();
    delete this;
    grpc_call_unref(call);
  }
}

void ServerContextBase::CompletionOp::FillOps(internal::Call* call) {
  grpc_op ops;
  ops.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  ops.data.recv_close_on_server.cancelled = &cancelled_;
  ops.flags = 0;
  ops.reserved = nullptr;
  interceptor_methods_.SetCall(&call_);
  interceptor_methods_.SetReverse();
  interceptor_methods_.SetCallOpSetInterface(this);
  // The following call_start_batch is internally-generated so no need for an
  // explanatory log on failure.
  GRPC_CHECK(grpc_call_start_batch(call->call(), &ops, 1, core_cq_tag_,
                                   nullptr) == GRPC_CALL_OK);
  // No interceptors to run here
}

bool ServerContextBase::CompletionOp::FinalizeResult(void** tag, bool* status) {
  // Decide whether to do the unref or call the cancel callback within the lock
  bool do_unref = false;
  bool has_tag = false;
  bool call_cancel = false;

  {
    grpc_core::MutexLock lock(&mu_);
    if (done_intercepting_) {
      // We are done intercepting.
      has_tag = has_tag_;
      if (has_tag) {
        *tag = tag_;
      }
      // Release the lock before unreffing as Unref may delete this object
      do_unref = true;
    } else {
      finalized_ = true;

      // If for some reason the incoming status is false, mark that as a
      // cancellation.
      // TODO(vjpai): does this ever happen?
      if (!*status) {
        cancelled_ = 1;
      }

      call_cancel = (cancelled_ != 0);
      // Release the lock since we may call a callback and interceptors.
    }
  }

  if (do_unref) {
    Unref();
    // Unref can delete this, so do not access anything from this afterward.
    return has_tag;
  }
  if (call_cancel && callback_controller_ != nullptr) {
    callback_controller_->MaybeCallOnCancel();
  }
  // Add interception point and run through interceptors
  interceptor_methods_.AddInterceptionHookPoint(
      experimental::InterceptionHookPoints::POST_RECV_CLOSE);
  if (interceptor_methods_.RunInterceptors()) {
    // No interceptors were run
    bool has_tag = has_tag_;
    if (has_tag) {
      *tag = tag_;
    }
    Unref();
    // Unref can delete this, so do not access anything from this afterward.
    return has_tag;
  }
  // There are interceptors to be run. Return false for now.
  return false;
}

// ServerContextBase body

ServerContextBase::ServerContextBase()
    : deadline_(gpr_inf_future(GPR_CLOCK_REALTIME)) {}

ServerContextBase::ServerContextBase(gpr_timespec deadline,
                                     grpc_metadata_array* arr)
    : deadline_(deadline) {
  std::swap(*client_metadata_.arr(), *arr);
}

void ServerContextBase::BindDeadlineAndMetadata(gpr_timespec deadline,
                                                grpc_metadata_array* arr) {
  deadline_ = deadline;
  std::swap(*client_metadata_.arr(), *arr);
}

ServerContextBase::~ServerContextBase() {
  if (completion_op_) {
    completion_op_->Unref();
    // Unref can delete completion_op_, so do not access it afterward.
  }
  if (rpc_info_) {
    rpc_info_->Unref();
  }
  if (default_reactor_used_.load(std::memory_order_relaxed)) {
    reinterpret_cast<Reactor*>(&default_reactor_)->~Reactor();
  }
  if (call_metric_recorder_ != nullptr) {
    call_metric_recorder_->~CallMetricRecorder();
  }
}

ServerContextBase::CallWrapper::~CallWrapper() {
  if (call) {
    // If the ServerContext is part of the call's arena, this could free the
    // object itself.
    grpc_call_unref(call);
  }
}

void ServerContextBase::BeginCompletionOp(
    internal::Call* call, std::function<void(bool)> callback,
    grpc::internal::ServerCallbackCall* callback_controller) {
  GRPC_CHECK(!completion_op_);
  if (rpc_info_) {
    rpc_info_->Ref();
  }
  grpc_call_ref(call->call());
  completion_op_ =
      new (grpc_call_arena_alloc(call->call(), sizeof(CompletionOp)))
          CompletionOp(call, callback_controller);
  if (callback_controller != nullptr) {
    completion_tag_.Set(call->call(), std::move(callback), completion_op_,
                        true);
    completion_op_->set_core_cq_tag(&completion_tag_);
    completion_op_->set_tag(completion_op_);
  } else if (has_notify_when_done_tag_) {
    completion_op_->set_tag(async_notify_when_done_tag_);
  }
  completion_op_->FillOps(call);
}

internal::CompletionQueueTag* ServerContextBase::GetCompletionOpTag() {
  return static_cast<internal::CompletionQueueTag*>(completion_op_);
}

void ServerContextBase::AddInitialMetadata(const std::string& key,
                                           const std::string& value) {
  initial_metadata_.insert(std::pair(key, value));
}

void ServerContextBase::AddTrailingMetadata(const std::string& key,
                                            const std::string& value) {
  trailing_metadata_.insert(std::pair(key, value));
}

void ServerContextBase::TryCancel() const {
  internal::CancelInterceptorBatchMethods cancel_methods;
  if (rpc_info_) {
    for (size_t i = 0; i < rpc_info_->interceptors_.size(); i++) {
      rpc_info_->RunInterceptor(&cancel_methods, i);
    }
  }
  grpc_call_error err =
      grpc_call_cancel_with_status(call_.call, GRPC_STATUS_CANCELLED,
                                   "Cancelled on the server side", nullptr);
  if (err != GRPC_CALL_OK) {
    LOG(ERROR) << "TryCancel failed with: " << err;
  }
}

bool ServerContextBase::IsCancelled() const {
  if (completion_tag_) {
    // When using callback API, this result is always valid.
    return marked_cancelled_.load(std::memory_order_acquire) ||
           completion_op_->CheckCancelledAsync();
  } else if (has_notify_when_done_tag_) {
    // When using async API, the result is only valid
    // if the tag has already been delivered at the completion queue
    return completion_op_ && completion_op_->CheckCancelledAsync();
  } else {
    // when using sync API, the result is always valid
    return marked_cancelled_.load(std::memory_order_acquire) ||
           (completion_op_ && completion_op_->CheckCancelled(cq_));
  }
}

void ServerContextBase::set_compression_algorithm(
    grpc_compression_algorithm algorithm) {
  compression_algorithm_ = algorithm;
  const char* algorithm_name = nullptr;
  if (!grpc_compression_algorithm_name(algorithm, &algorithm_name)) {
    grpc_core::Crash(absl::StrFormat(
        "Name for compression algorithm '%d' unknown.", algorithm));
  }
  GRPC_CHECK_NE(algorithm_name, nullptr);
  AddInitialMetadata(GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY, algorithm_name);
}

std::string ServerContextBase::peer() const {
  std::string peer;
  if (call_.call) {
    char* c_peer = grpc_call_get_peer(call_.call);
    peer = c_peer;
    gpr_free(c_peer);
  }
  return peer;
}

const struct census_context* ServerContextBase::census_context() const {
  return call_.call == nullptr ? nullptr
                               : grpc_census_call_get_context(call_.call);
}

void ServerContextBase::SetLoadReportingCosts(
    const std::vector<std::string>& cost_data) {
  if (call_.call == nullptr) return;
  for (const auto& cost_datum : cost_data) {
    AddTrailingMetadata(GRPC_LB_COST_MD_KEY, cost_datum);
  }
}

void ServerContextBase::CreateCallMetricRecorder(
    experimental::ServerMetricRecorder* server_metric_recorder) {
  if (call_.call == nullptr) return;
  GRPC_CHECK_EQ(call_metric_recorder_, nullptr);
  grpc_core::Arena* arena = grpc_call_get_arena(call_.call);
  auto* backend_metric_state =
      arena->New<BackendMetricState>(server_metric_recorder);
  call_metric_recorder_ = backend_metric_state;
  arena->SetContext<grpc_core::BackendMetricProvider>(backend_metric_state);
}

grpc::string_ref ServerContextBase::ExperimentalGetAuthority() const {
  absl::string_view authority = grpc_call_server_authority(call_.call);
  return grpc::string_ref(authority.data(), authority.size());
}

}  // namespace grpc
