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

#include <openssl/ssl.h>

#include <cstdint>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {
    struct OnSignCompleteData {
        absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_sign_complete;
    };
}  // namespace

    void OnSignCompletePyWrapperImpl(const absl::StatusOr<std::string> signed_data, void* completion_data) {
        auto* data = static_cast<OnSignCompleteData*>(completion_data);
        data->on_sign_complete(signed_data);
        delete data;
    }

    void CustomPrivateKeySignerPyWrapper::Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm, OnSignComplete on_sign_complete) {
        // Use an input SignPyWrapper
        OnSignCompleteData* data = new OnSignCompleteData{std::move(on_sign_complete)};
        sign_py_wrapper_(data_to_sign, signature_algorithm, OnSignCompletePyWrapperImpl, data, sign_user_data_);
    }
}  // namespace grpc_core 


// namespace {
// struct OnSignCompleteData {
//   absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_sign_complete;
// };
// }  // namespace

// // This function is called by Python wrapper when signing is complete.
// void PyOnSignComplete(const absl::StatusOr<std::string>& signature,
//                       void* completion_data) {
//   auto* data = static_cast<OnSignCompleteData*>(completion_data);
//   data->on_sign_complete(signature);
//   delete data;
// }

// PyCustomPrivateKeySigner::PyCustomPrivateKeySigner(
//     grpc_private_key_offload_py_sign_cb sign_cb, void* user_data)
//     : sign_cb_(sign_cb), user_data_(user_data) {}

// void PyCustomPrivateKeySigner::Sign(absl::string_view data_to_sign,
//                                     SignatureAlgorithm signature_algorithm,
//                                     OnSignComplete on_sign_complete) {
//   OnSignCompleteData* data =
//       new OnSignCompleteData{std::move(on_sign_complete)};
//   sign_cb_(std::string(data_to_sign), static_cast<int>(signature_algorithm),
//            PyOnSignComplete, data, user_data_);
// }