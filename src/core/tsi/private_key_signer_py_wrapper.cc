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

// #include <Python.h>
#include <grpc/support/log.h>

#include "grpc/private_key_signer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
// #include "src/core/util/match.h"

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
  auto* completion_context = new CompletionContext{std::move(on_sign_complete)};
  PrivateKeySignerPyWrapperResult result =
      sign_py_wrapper_(data_to_sign, signature_algorithm, sign_user_data_,
                       CompletionCallbackForPy, completion_context);
  if (result.async_handle != nullptr) {
    return result.async_handle;
  } else {
    return result.sync_result;
  }
}

void PrivateKeySignerPyWrapper::Cancel(
    std::shared_ptr<AsyncSigningHandle>) { /* TODO(gregorycooke) will need to
                                              bubble up to Python? */
}

std::shared_ptr<PrivateKeySigner> BuildPrivateKeySigner(
    SignWrapperForPy sign_py_wrapper, void* user_data) {
  return std::make_shared<PrivateKeySignerPyWrapper>(sign_py_wrapper,
                                                     user_data);
}

}  // namespace grpc_core
