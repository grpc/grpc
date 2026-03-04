//
//
// Copyright 2026 gRPC authors.
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

#include "Python.h"
#include "grpc/private_key_signer.h"
#include "absl/status/statusor.h"

namespace grpc_core {

// A C-style callback for the PrivateKeySigner Cancel function.
typedef void (*CancelWrapperForPy)(void* cancel_data);

// A wrapper for holding the user's Python cancellation function as well as the
// C callback that can call that function.
struct AsyncResult {
  CancelWrapperForPy cancel_wrapper;
  void* py_user_cancel_fn;
};

// A C-Style callback for the Completion function that will be passed to the
// cython layer.
typedef void (*CompletionFunctionPyWrapper)(absl::StatusOr<std::string> result,
                                            void* c_on_complete_fn);

// The result of the sign call for interop between Cython and C. Is converted to
// the C++ std::variant Sign result.
struct PrivateKeySignerPyWrapperResult {
  absl::StatusOr<std::string> sync_result;
  AsyncResult async_result;
  bool is_sync;
};

// The context needed for calling the Completion callback at the Cython layer.
// Wrapped in regular Python and passed to the user for them to be able to call
// the proper on_complete callback passed out by gRPC Core.
class CompletionContext {
 public:
  explicit CompletionContext(
      grpc_core::PrivateKeySigner::OnSignComplete on_complete)
      : on_complete_(std::move(on_complete)) {}
  void OnComplete(absl::StatusOr<std::string> result) {
    on_complete_(std::move(result));
  };

 private:
  // Holds the completion function passed out by gRPC Core.
  grpc_core::PrivateKeySigner::OnSignComplete on_complete_;
};

// A C-Style function for the Cython layer to call when the gRPC C++ layer calls
// `Sign` on the `PrivateKeySignerPyWrapper`.
typedef PrivateKeySignerPyWrapperResult (*SignWrapperForPy)(
    absl::string_view data_to_sign,
    grpc_core::PrivateKeySigner::SignatureAlgorithm signature_algorithm,
    void* py_user_sign_fn,
    std::unique_ptr<CompletionContext> completion_context);

// An implementation of PrivateKeySigner for interop with Python.
// It is thread-safe to call Sign on this class.
class PrivateKeySignerPyWrapper
    : public PrivateKeySigner,
      public std::enable_shared_from_this<PrivateKeySignerPyWrapper> {
 public:
  PrivateKeySignerPyWrapper(SignWrapperForPy sign_py_wrapper,
                            void* py_user_sign_fn, PyObject* destroy_event)
      : sign_py_wrapper_(sign_py_wrapper),
        py_user_sign_fn(py_user_sign_fn),
        destroy_event_(destroy_event) {}
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
  // An event to make sure the python interpreter stays alive until this
  // destruction is complete
  PyObject* destroy_event_;
};

// The entry point for Cython to build a PrivateKeySigner.
std::shared_ptr<PrivateKeySigner> BuildPrivateKeySigner(
    SignWrapperForPy sign, void* py_user_sign_fn, PyObject* destroy_event);

class AsyncSigningHandlePyWrapper : public PrivateKeySigner::AsyncSigningHandle {
 public:
  AsyncSigningHandlePyWrapper(CancelWrapperForPy cancel_py_wrapper,
                              void* py_user_cancel_fn)
      : cancel_py_wrapper_(cancel_py_wrapper),
        py_user_cancel_fn_(py_user_cancel_fn) {}
  // This will decrememnt the py_user_cancel_fn on object destruction
  ~AsyncSigningHandlePyWrapper() override;
  void Cancel();

 private:
  // This is a function provided by the Cython implementation of Private Key
  // Offloading.
  CancelWrapperForPy cancel_py_wrapper_;
  // This will hold the Python callable object
  void* py_user_cancel_fn_;
};

// Python cannot call the string constructor directly in Cython. The string
// constructor can throw exceptions, so the generated C code from Cython
// contains try/catch statements. This fails our strict builds. Instead, we can
// just construct them here and pass them down.
std::string MakeStringForCython(const char* inp);
std::string MakeStringForCython(const char* inp, size_t size);
}  // namespace grpc_core

#endif  // GRPC_PRIVATE_KEY_SIGNER_PY_WRAPPER_H
