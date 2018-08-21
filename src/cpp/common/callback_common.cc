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
  CallbackWithSuccessImpl(CallbackWithSuccessTag* parent,
                          std::function<void(bool)> f, bool self_delete)
      : parent_(parent), func_(f), self_delete_(self_delete) {}

  void Run(bool ok) override {
    void* ignored = parent_->ops();
    bool new_ok = ok;
    GPR_ASSERT(parent_->ops()->FinalizeResult(&ignored, &new_ok));
    GPR_ASSERT(ignored == parent_->ops());
    func_(ok);
    if (self_delete_) {
      delete parent_;
      // Must use grpc_core::Delete since base is GRPC_ABSTRACT
      grpc_core::Delete(this);
    }
  }

 private:
  CallbackWithSuccessTag* parent_;
  std::function<void(bool)> func_;
  bool self_delete_;
};

class CallbackWithStatusImpl : public grpc_core::CQCallbackInterface {
 public:
  CallbackWithStatusImpl(CallbackWithStatusTag* parent,
                         std::function<void(Status)> f, bool self_delete)
      : parent_(parent), func_(f), status_(), self_delete_(self_delete) {}

  void Run(bool ok) override {
    void* ignored = parent_->ops();

    GPR_ASSERT(parent_->ops()->FinalizeResult(&ignored, &ok));
    GPR_ASSERT(ignored == parent_->ops());

    func_(status_);
    if (self_delete_) {
      delete parent_;
      // Must use grpc_core::Delete since base is GRPC_ABSTRACT
      grpc_core::Delete(this);
    }
  }
  Status* status_ptr() { return &status_; }

 private:
  CallbackWithStatusTag* parent_;
  std::function<void(Status)> func_;
  Status status_;
  bool self_delete_;
};

}  // namespace

CallbackWithSuccessTag::CallbackWithSuccessTag(std::function<void(bool)> f,
                                               bool self_delete,
                                               CompletionQueueTag* ops)
    : impl_(grpc_core::New<CallbackWithSuccessImpl>(this, f, self_delete)),
      ops_(ops) {}

void CallbackWithSuccessTag::force_run(bool ok) { impl_->Run(ok); }

CallbackWithStatusTag::CallbackWithStatusTag(std::function<void(Status)> f,
                                             bool self_delete,
                                             CompletionQueueTag* ops)
    : ops_(ops) {
  auto* impl = grpc_core::New<CallbackWithStatusImpl>(this, f, self_delete);
  impl_ = impl;
  status_ = impl->status_ptr();
}

void CallbackWithStatusTag::force_run(Status s) {
  *status_ = s;
  impl_->Run(true);
}

}  // namespace internal
}  // namespace grpc
