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

#ifndef GRPCPP_IMPL_CODEGEN_CALLBACK_COMMON_H
#define GRPCPP_IMPL_CODEGEN_CALLBACK_COMMON_H

#include <functional>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/channel_interface.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/status.h>

namespace grpc {
namespace internal {

/// An exception-safe way of invoking a user-specified callback function
template <class Func, class... Args>
void CatchingCallback(Func&& func, Args&&... args) {
#if GRPC_ALLOW_EXCEPTIONS
  try {
    func(std::forward<Args>(args)...);
  } catch (...) {
    // nothing to return or change here, just don't crash the library
  }
#else   // GRPC_ALLOW_EXCEPTIONS
  func(std::forward<Args>(args)...);
#endif  // GRPC_ALLOW_EXCEPTIONS
}

// The contract on these tags is that they are single-shot. They must be
// constructed and then fired at exactly one point. There is no expectation
// that they can be reused without reconstruction.

class CallbackWithStatusTag
    : public grpc_experimental_completion_queue_functor {
 public:
  // always allocated against a call arena, no memory free required
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(CallbackWithStatusTag));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

  CallbackWithStatusTag(grpc_call* call, std::function<void(Status)> f,
                        CompletionQueueTag* ops)
      : call_(call), func_(std::move(f)), ops_(ops) {
    g_core_codegen_interface->grpc_call_ref(call);
    functor_run = &CallbackWithStatusTag::StaticRun;
  }
  ~CallbackWithStatusTag() {}
  Status* status_ptr() { return &status_; }

  // force_run can not be performed on a tag if operations using this tag
  // have been sent to PerformOpsOnCall. It is intended for error conditions
  // that are detected before the operations are internally processed.
  void force_run(Status s) {
    status_ = std::move(s);
    Run(true);
  }

 private:
  grpc_call* call_;
  std::function<void(Status)> func_;
  CompletionQueueTag* ops_;
  Status status_;

  static void StaticRun(grpc_experimental_completion_queue_functor* cb,
                        int ok) {
    static_cast<CallbackWithStatusTag*>(cb)->Run(static_cast<bool>(ok));
  }
  void Run(bool ok) {
    void* ignored = ops_;

    if (!ops_->FinalizeResult(&ignored, &ok)) {
      // The tag was swallowed
      return;
    }
    GPR_CODEGEN_ASSERT(ignored == ops_);

    // Last use of func_ or status_, so ok to move them out
    auto func = std::move(func_);
    auto status = std::move(status_);
    func_ = nullptr;     // reset to clear this out for sure
    status_ = Status();  // reset to clear this out for sure
    CatchingCallback(std::move(func), std::move(status));
    g_core_codegen_interface->grpc_call_unref(call_);
  }
};

/// CallbackWithSuccessTag can be reused multiple times, and will be used in
/// this fashion for streaming operations. As a result, it shouldn't clear
/// anything up until its destructor
class CallbackWithSuccessTag
    : public grpc_experimental_completion_queue_functor {
 public:
  // always allocated against a call arena, no memory free required
  static void operator delete(void* ptr, std::size_t size) {
    assert(size == sizeof(CallbackWithSuccessTag));
  }

  // This operator should never be called as the memory should be freed as part
  // of the arena destruction. It only exists to provide a matching operator
  // delete to the operator new so that some compilers will not complain (see
  // https://github.com/grpc/grpc/issues/11301) Note at the time of adding this
  // there are no tests catching the compiler warning.
  static void operator delete(void*, void*) { assert(0); }

  CallbackWithSuccessTag() : call_(nullptr) {}

  CallbackWithSuccessTag(grpc_call* call, std::function<void(bool)> f,
                         CompletionQueueTag* ops) {
    Set(call, f, ops);
  }

  CallbackWithSuccessTag(const CallbackWithSuccessTag&) = delete;
  CallbackWithSuccessTag& operator=(const CallbackWithSuccessTag&) = delete;

  ~CallbackWithSuccessTag() { Clear(); }

  // Set can only be called on a default-constructed or Clear'ed tag.
  // It should never be called on a tag that was constructed with arguments
  // or on a tag that has been Set before unless the tag has been cleared.
  void Set(grpc_call* call, std::function<void(bool)> f,
           CompletionQueueTag* ops) {
    call_ = call;
    func_ = std::move(f);
    ops_ = ops;
    g_core_codegen_interface->grpc_call_ref(call);
    functor_run = &CallbackWithSuccessTag::StaticRun;
  }

  void Clear() {
    if (call_ != nullptr) {
      func_ = nullptr;
      grpc_call* call = call_;
      call_ = nullptr;
      g_core_codegen_interface->grpc_call_unref(call);
    }
  }

  CompletionQueueTag* ops() { return ops_; }

  // force_run can not be performed on a tag if operations using this tag
  // have been sent to PerformOpsOnCall. It is intended for error conditions
  // that are detected before the operations are internally processed.
  void force_run(bool ok) { Run(ok); }

  /// check if this tag is currently set
  operator bool() const { return call_ != nullptr; }

 private:
  grpc_call* call_;
  std::function<void(bool)> func_;
  CompletionQueueTag* ops_;

  static void StaticRun(grpc_experimental_completion_queue_functor* cb,
                        int ok) {
    static_cast<CallbackWithSuccessTag*>(cb)->Run(static_cast<bool>(ok));
  }
  void Run(bool ok) {
    void* ignored = ops_;
    bool new_ok = ok;
    // Allow a "false" return value from FinalizeResult to silence the
    // callback, just as it silences a CQ tag in the async cases
    bool do_callback = ops_->FinalizeResult(&ignored, &new_ok);
    GPR_CODEGEN_ASSERT(ignored == ops_);

    if (do_callback) {
      CatchingCallback(func_, ok);
    }
  }
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CALLBACK_COMMON_H
