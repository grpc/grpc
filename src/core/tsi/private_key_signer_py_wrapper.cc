//
//
// Copyright 2025 gRPC authors.
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

#include "src/core/tsi/private_key_signer_py_wrapper.h"

#include <grpc/support/log.h>

#include <memory>

#include "grpc/private_key_signer.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

void CompletionCallbackForPy(absl::StatusOr<std::string> result,
                             void* completion_data) {
  CompletionContext* context = static_cast<CompletionContext*>(completion_data);
  context->on_complete(result);
  delete context;
}

std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
PrivateKeySignerPyWrapper::Sign(absl::string_view data_to_sign,
                                SignatureAlgorithm signature_algorithm,
                                OnSignComplete on_sign_complete) {
  LOG(INFO) << "GREG: In sign\n";
  auto* completion_context = new CompletionContext{std::move(on_sign_complete)};
  PrivateKeySignerPyWrapperResult result =
      sign_py_wrapper_(data_to_sign, signature_algorithm, sign_user_data_,
                       CompletionCallbackForPy, completion_context);
  if (result.is_sync) {
    LOG(INFO) << "GREG: In Sync Return\n";
    return result.sync_result;
  } else {
    LOG(INFO) << "GREG: In new cancel return\n";
    auto handle = std::make_shared<AsyncSigningHandlePyWrapper>();
    handle->cancel_py_wrapper_ = result.async_result.cancel_wrapper;
    handle->python_callable = result.async_result.python_callable;
    handle->python_callable_decref = result.async_result.python_callable_decref;
    LOG(INFO) << "GREG: Returning Async handle\n";
    return handle;
  }
}

void PrivateKeySignerPyWrapper::Cancel(
    std::shared_ptr<AsyncSigningHandle> handle) {
  LOG(INFO) << "GREG: In PyWrapper::Cancel\n";
  auto handle_impl =
      std::static_pointer_cast<AsyncSigningHandlePyWrapper>(handle);
  if (handle == nullptr || handle_impl->cancel_py_wrapper_ == nullptr) {
    return;
  }
  handle_impl->cancel_py_wrapper_(handle_impl->python_callable);
}

std::shared_ptr<PrivateKeySigner> BuildPrivateKeySigner(
    SignWrapperForPy sign_py_wrapper, void* user_data) {
  return std::make_shared<PrivateKeySignerPyWrapper>(sign_py_wrapper,
                                                     user_data);
}

std::shared_ptr<PrivateKeySigner> BuildPrivateKeySignerWithCancellation(
    SignWrapperForPy sign_py_wrapper, void* user_data,
    CancelWrapperForPy cancel_py_wrapper_, void* cancel_user_data) {
  return std::make_shared<PrivateKeySignerPyWrapper>(
      sign_py_wrapper, user_data, cancel_py_wrapper_, cancel_user_data);
}

AsyncSigningHandlePyWrapper::~AsyncSigningHandlePyWrapper() {
  python_callable_decref(python_callable);
}

}  // namespace grpc_core
