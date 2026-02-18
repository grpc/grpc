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

typedef void (*CancelWrapperForPy)(void* cancel_data);
typedef void (*PythonCallableDecref)(void* py_user_cancel_fn);
struct AsyncResult {
  CancelWrapperForPy cancel_wrapper;
  void* py_user_cancel_fn;
};

struct PrivateKeySignerPyWrapperResult {
  absl::StatusOr<std::string> sync_result;
  AsyncResult async_result;
  bool is_sync;
};

typedef void (*CompletionFunctionPyWrapper)(absl::StatusOr<std::string> result,
                                            void* c_on_complete_fn);

class CompletionContext {
 public:
  explicit CompletionContext(
      grpc_core::PrivateKeySigner::OnSignComplete on_complete)
      : on_complete_(std::move(on_complete)) {}
  void OnComplete(absl::StatusOr<std::string> result) { on_complete_(result); };

 private:
  grpc_core::PrivateKeySigner::OnSignComplete on_complete_;
};

typedef PrivateKeySignerPyWrapperResult (*SignWrapperForPy)(
    absl::string_view data_to_sign,
    grpc_core::PrivateKeySigner::SignatureAlgorithm signature_algorithm,
    void* py_user_sign_fn,
    std::unique_ptr<CompletionContext> completion_context);

// An implementation of PrivateKeySigner for interop with Python.
class PrivateKeySignerPyWrapper
    : public PrivateKeySigner,
      public std::enable_shared_from_this<PrivateKeySignerPyWrapper> {
 public:
  PrivateKeySignerPyWrapper(SignWrapperForPy sign_py_wrapper,
                            void* py_user_sign_fn)
      : sign_py_wrapper_(sign_py_wrapper), py_user_sign_fn(py_user_sign_fn) {}
  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete on_sign_complete) override;

  ~PrivateKeySignerPyWrapper() override;

  void Cancel(std::shared_ptr<AsyncSigningHandle> handle) override;

 private:
  // This is a function provided by the Cython implementation of Private Key
  // Offloading.
  SignWrapperForPy sign_py_wrapper_;
  // This will hold the Python callable object
  void* py_user_sign_fn;
};

// The entry point for Cython to build a PrivateKeySigner.
std::shared_ptr<PrivateKeySigner> BuildPrivateKeySigner(SignWrapperForPy sign,
                                                        void* py_user_sign_fn);

class AsyncSigningHandlePyWrapper : public PrivateKeySigner::AsyncSigningHandle {
 public:
  // This is a function provided by the Cython implementation of Private Key
  // Offloading.
  CancelWrapperForPy cancel_py_wrapper_;
  // This will hold the Python callable object
  void* py_user_cancel_fn;
  // This will decrememnt the py_user_cancel_fn on object destruction
  ~AsyncSigningHandlePyWrapper() override;
};
}  // namespace grpc_core

#endif  // GRPC_PRIVATE_KEY_SIGNER_PY_WRAPPER_H
