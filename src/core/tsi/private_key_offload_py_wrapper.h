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

#ifndef GRPC_GRPC_PRIVATE_KEY_OFFLOAD_PY_WRAPPER_H
#define GRPC_GRPC_PRIVATE_KEY_OFFLOAD_PY_WRAPPER_H

#include <string>

#include "grpc/private_key_signer.h"
#include "absl/status/statusor.h"

// This needs to be an impl of `PrivateKeySigner`

namespace grpc_core {
    typedef void (*OnSignCompletePyWrapper)(const absl::StatusOr<std::string> result, void* completion_data);
    typedef void (*SignPyWrapper)(
        absl::string_view data_to_sign,
        grpc_core::PrivateKeySigner::SignatureAlgorithm signature_algorithm,
        OnSignCompletePyWrapper on_sign_complete_py_wrapper,
        void* completion_data, void* user_data);

    class PrivateKeySignerPyWrapper : public PrivateKeySigner {
     public:
      PrivateKeySignerPyWrapper(SignPyWrapper sign_py_wrapper, void* user_data)
          : sign_py_wrapper_(sign_py_wrapper), sign_user_data_(user_data) {}
      void Sign(absl::string_view data_to_sign,
                SignatureAlgorithm signature_algorithm,
                OnSignComplete on_sign_complete) override;

     private:
      SignPyWrapper sign_py_wrapper_;
      void* sign_user_data_;
    };

    PrivateKeySigner* BuildPrivateKeySigner(SignPyWrapper sign,
                                            void* user_data);
    }  // namespace grpc_core

#endif  // GRPC_GRPC_PRIVATE_KEY_OFFLOAD_PY_WRAPPER_H