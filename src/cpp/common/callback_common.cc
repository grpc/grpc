/*
 *
 * Copyright 2018 gRPC authors.
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

#include <functional>

#include <grpcpp/impl/codegen/callback_common.h>
#include <grpcpp/impl/codegen/status.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/surface/completion_queue.h"

namespace grpc {
namespace internal {

namespace {
class CallbackWithSuccessImpl : public grpc_core::CQCallbackInterface {
 public:
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(CallbackWithSuccessImpl));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

  CallbackWithSuccessImpl(grpc_call* call, CallbackWithSuccessTag* parent,
                          std::function<void(bool)> f)
      : call_(call), parent_(parent), func_(std::move(f)) {
    grpc_call_ref(call);
  }

  void Run(bool ok) override {
    void* ignored = parent_->ops();
    bool new_ok = ok;
    GPR_ASSERT(parent_->ops()->FinalizeResult(&ignored, &new_ok));
    GPR_ASSERT(ignored == parent_->ops());
    func_(ok);
    func_ = nullptr;  // release the function
    grpc_call_unref(call_);
  }

 private:
  grpc_call* call_;
  CallbackWithSuccessTag* parent_;
  std::function<void(bool)> func_;
};

class CallbackWithStatusImpl : public grpc_core::CQCallbackInterface {
 public:
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(CallbackWithStatusImpl));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

  CallbackWithStatusImpl(grpc_call* call, CallbackWithStatusTag* parent,
                         std::function<void(Status)> f)
      : call_(call), parent_(parent), func_(std::move(f)), status_() {
    grpc_call_ref(call);
  }

  void Run(bool ok) override {
    void* ignored = parent_->ops();

    GPR_ASSERT(parent_->ops()->FinalizeResult(&ignored, &ok));
    GPR_ASSERT(ignored == parent_->ops());

    func_(status_);
    func_ = nullptr;  // release the function
    grpc_call_unref(call_);
  }
  Status* status_ptr() { return &status_; }

 private:
  grpc_call* call_;
  CallbackWithStatusTag* parent_;
  std::function<void(Status)> func_;
  Status status_;
};

}  // namespace

CallbackWithSuccessTag::CallbackWithSuccessTag(grpc_call* call,
                                               std::function<void(bool)> f,
                                               CompletionQueueTag* ops)
    : impl_(new (grpc_call_arena_alloc(call, sizeof(CallbackWithSuccessImpl)))
                CallbackWithSuccessImpl(call, this, std::move(f))),
      ops_(ops) {}

void CallbackWithSuccessTag::force_run(bool ok) { impl_->Run(ok); }

CallbackWithStatusTag::CallbackWithStatusTag(grpc_call* call,
                                             std::function<void(Status)> f,
                                             CompletionQueueTag* ops)
    : ops_(ops) {
  auto* impl = new (grpc_call_arena_alloc(call, sizeof(CallbackWithStatusImpl)))
      CallbackWithStatusImpl(call, this, std::move(f));
  impl_ = impl;
  status_ = impl->status_ptr();
}

void CallbackWithStatusTag::force_run(Status s) {
  *status_ = std::move(s);
  impl_->Run(true);
}

}  // namespace internal
}  // namespace grpc
