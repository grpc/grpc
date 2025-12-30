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

#include "src/core/tsi/private_key_offload_py_wrapper.h"

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

void PrivateKeySignerPyWrapper::Sign(absl::string_view data_to_sign,
                                     SignatureAlgorithm signature_algorithm,
                                     OnSignComplete on_sign_complete) {
  auto on_sign_complete_cpp_callback =
      [](const absl::StatusOr<std::string> result, void* completion_data) {
        grpc_core::ExecCtx exec_ctx;
        auto* on_sign_complete_ptr =
            static_cast<OnSignComplete*>(completion_data);
        (*on_sign_complete_ptr)(result);
        delete on_sign_complete_ptr;
      };

  // We have to manage the lifetime
  auto* on_sign_complete_heap =
      new OnSignComplete(std::move(on_sign_complete));

  sign_py_wrapper_(data_to_sign, signature_algorithm,
                   on_sign_complete_cpp_callback, on_sign_complete_heap,
                   sign_user_data_);
}

PrivateKeySigner* BuildPrivateKeySigner(SignPyWrapper sign_py_wrapper,
                                        void* user_data) {
  return new PrivateKeySignerPyWrapper(sign_py_wrapper, user_data);
}

}  // namespace grpc_core
