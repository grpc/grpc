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

#include "src/python/grpcio/grpc/_cython/_cygrpc/private_key_signing/private_key_signer_py_wrapper.h"

#include <grpc/support/log.h>

#include <memory>

#include "Python.h"
#include "grpc/private_key_signer.h"

namespace grpc_core {

std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
PrivateKeySignerPyWrapper::Sign(absl::string_view data_to_sign,
                                SignatureAlgorithm signature_algorithm,
                                OnSignComplete on_sign_complete) {
  auto completion_context =
      std::make_unique<CompletionContext>(std::move(on_sign_complete));

  PrivateKeySignerPyWrapperResult result =
      sign_py_wrapper_(data_to_sign, signature_algorithm, py_user_sign_fn,
                       std::move(completion_context));
  if (result.is_sync) {
    return result.sync_result;
  } else {
    auto handle = std::make_shared<AsyncSigningHandlePyWrapper>();
    handle->cancel_py_wrapper_ = result.async_result.cancel_wrapper;
    handle->py_user_cancel_fn = result.async_result.py_user_cancel_fn;
    return handle;
  }
}

void PrivateKeySignerPyWrapper::Cancel(
    std::shared_ptr<AsyncSigningHandle> handle) {
  auto handle_impl =
      std::static_pointer_cast<AsyncSigningHandlePyWrapper>(handle);
  if (handle == nullptr || handle_impl->cancel_py_wrapper_ == nullptr) {
    return;
  }
  handle_impl->cancel_py_wrapper_(handle_impl->py_user_cancel_fn);
}

std::shared_ptr<PrivateKeySigner> BuildPrivateKeySigner(
  SignWrapperForPy sign_py_wrapper, void* py_user_sign_fn) {
  PyGILState_STATE state = PyGILState_Ensure();
  Py_INCREF(static_cast<PyObject*>(py_user_sign_fn));
  PyGILState_Release(state);
  return std::make_shared<PrivateKeySignerPyWrapper>(sign_py_wrapper,
                                                     py_user_sign_fn);
}

AsyncSigningHandlePyWrapper::~AsyncSigningHandlePyWrapper() {
  PyGILState_STATE state = PyGILState_Ensure();
  Py_DECREF(py_user_cancel_fn);
  PyGILState_Release(state);
}

PrivateKeySignerPyWrapper::~PrivateKeySignerPyWrapper() {
  PyGILState_STATE state = PyGILState_Ensure();
  Py_DECREF(static_cast<PyObject*>(py_user_sign_fn));
  PyGILState_Release(state);
}

}  // namespace grpc_core
