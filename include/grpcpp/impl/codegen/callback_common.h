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

#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/channel_interface.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/status.h>

// Forward declarations
namespace grpc_core {
class CQCallbackInterface;
};

namespace grpc {
namespace internal {

class CallbackWithStatusTag {
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
                        CompletionQueueTag* ops);
  ~CallbackWithStatusTag() {}
  void* tag() { return static_cast<void*>(impl_); }
  Status* status_ptr() { return status_; }
  CompletionQueueTag* ops() { return ops_; }

  // force_run can not be performed on a tag if operations using this tag
  // have been sent to PerformOpsOnCall. It is intended for error conditions
  // that are detected before the operations are internally processed.
  void force_run(Status s);

 private:
  grpc_core::CQCallbackInterface* impl_;
  Status* status_;
  CompletionQueueTag* ops_;
};

class CallbackWithSuccessTag {
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

  CallbackWithSuccessTag(grpc_call* call, std::function<void(bool)> f,
                         CompletionQueueTag* ops);

  void* tag() { return static_cast<void*>(impl_); }
  CompletionQueueTag* ops() { return ops_; }

  // force_run can not be performed on a tag if operations using this tag
  // have been sent to PerformOpsOnCall. It is intended for error conditions
  // that are detected before the operations are internally processed.
  void force_run(bool ok);

 private:
  grpc_core::CQCallbackInterface* impl_;
  CompletionQueueTag* ops_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CALLBACK_COMMON_H
