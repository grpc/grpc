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

#ifndef GRPC_PRIVATE_KEY_SIGNER_PY_WRAPPER_H
#define GRPC_PRIVATE_KEY_SIGNER_PY_WRAPPER_H

#include <memory>
#include <string>
#include <variant>

#include "grpc/private_key_signer.h"
#include "absl/status/statusor.h"

namespace grpc_core {

struct PrivateKeySignerPyWrapperResult {
  absl::StatusOr<std::string> sync_result;
  std::shared_ptr<AsyncSigningHandle> async_handle;
};

typedef void (*CompletionFunctionPyWrapper)(absl::StatusOr<std::string> result,
                                            void* completion_data);

typedef void (*CancelWrapperForPy)(std::shared_ptr<AsyncSigningHandle>,
                                   void* cancel_data);

typedef PrivateKeySignerPyWrapperResult (*SignWrapperForPy)(
    absl::string_view data_to_sign,
    grpc_core::PrivateKeySigner::SignatureAlgorithm signature_algorithm,
    void* user_data, CompletionFunctionPyWrapper on_complete,
    void* completion_data);

struct CompletionContext {
  PrivateKeySigner::OnSignComplete on_complete;
};

// An implementation of PrivateKeySigner for interop with Python.
class PrivateKeySignerPyWrapper
    : public PrivateKeySigner,
      public std::enable_shared_from_this<PrivateKeySignerPyWrapper> {
 public:
  PrivateKeySignerPyWrapper(SignWrapperForPy sign_py_wrapper, void* user_data)
      : sign_py_wrapper_(sign_py_wrapper), sign_user_data_(user_data) {}
  PrivateKeySignerPyWrapper(SignWrapperForPy sign_py_wrapper, void* user_data,
                            CancelWrapperForPy cancel_py_wrapper,
                            void* cancel_data)
      : sign_py_wrapper_(sign_py_wrapper),
        sign_user_data_(user_data),
        cancel_py_wrapper_(cancel_py_wrapper),
        cancel_user_data_(cancel_data) {}
  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete on_sign_complete) override;

  void Cancel(std::shared_ptr<AsyncSigningHandle> handle) override;

 private:
  // This is a function provided by the Cython implementation of Private Key
  // Offloading.
  SignWrapperForPy sign_py_wrapper_;
  // This will hold the Python callable object
  void* sign_user_data_;
  // This is a function provided by the Cython implementation of Private Key
  // Offloading.
  CancelWrapperForPy cancel_py_wrapper_;
  // THis will hold the Python callable object
  void* cancel_user_data_;
};

// The entry point for Cython to build a PrivateKeySigner.
std::shared_ptr<PrivateKeySigner> BuildPrivateKeySigner(SignWrapperForPy sign,
                                                        void* user_data);

std::shared_ptr<PrivateKeySigner> BuildPrivateKeySignerWithCancellation(
    SignWrapperForPy sign_py_wrapper, void* user_data,
    CancelWrapperForPy cancel_py_wrapper_, void* cancel_user_data);

class AsyncSigningHandlePyWrapper : public AsyncSigningHandle {
 public:
  // The Python object that the user creates in their implementation
  void* python_handle;
};
}  // namespace grpc_core

#endif  // GRPC_PRIVATE_KEY_SIGNER_PY_WRAPPER_H