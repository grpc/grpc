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
  // TODO(vjpai): make impl and ops part of this structure to avoid allocation,
  // ownership transfer, and delete
  CallbackWithStatusTag(std::function<void(Status)> f, bool self_delete,
                        CompletionQueueTag* ops);
  ~CallbackWithStatusTag() { delete ops_; }
  void* tag() { return static_cast<void*>(impl_); }
  Status* status_ptr() { return status_; }
  CompletionQueueTag* ops() { return ops_; }

  // force_run can only be performed on a tag before it can ever be active
  void force_run(Status s);

 private:
  grpc_core::CQCallbackInterface* impl_;
  Status* status_;
  CompletionQueueTag* ops_;
};

class CallbackWithSuccessTag {
 public:
  // TODO(vjpai): make impl and ops part of this structure to avoid allocation,
  // ownership transfer, and delete
  CallbackWithSuccessTag(std::function<void(bool)> f, bool self_delete,
                         CompletionQueueTag* ops);
  ~CallbackWithSuccessTag() { delete ops_; }
  void* tag() { return static_cast<void*>(impl_); }
  CompletionQueueTag* ops() { return ops_; }

  // force_run can only be performed on a tag before it can ever be active
  void force_run(bool ok);

 private:
  grpc_core::CQCallbackInterface* impl_;
  CompletionQueueTag* ops_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CALLBACK_COMMON_H
