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

namespace grpc_core {

// This struct is used to pass the C++ move-only AnyInvocable through
// the C-style void* pointer API.
// struct CompletionContext {
//   OnSignComplete on_complete;
// };

// TODO(get this working) - mixing async and non-async stuff I think
std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
PrivateKeySignerPyWrapper::Sign(absl::string_view data_to_sign,
                                SignatureAlgorithm signature_algorithm,
                                OnSignComplete on_sign_complete) {
  // We have a fn, sign_py_wrapper_ of type SignWrapperForPy to call
  // Wrap it with the C++ APIs
  // auto* context = new CompletionContext{
  //     .on_complete = std::move(on_sign_complete),
  // };
  LOG(WARNING) << "GREG: Sign\n";
  auto event_engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  event_engine->Run([self = shared_from_this(),
                     data_to_sign = std::string(data_to_sign),
                     signature_algorithm,
                     on_complete = std::move(on_sign_complete)]() mutable {
    // gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
    //                              gpr_time_from_millis(3000, GPR_TIMESPAN)));
    // Make sure it's safe to call into Python
    // PyGILState_STATE gstate;
    // gstate = PyGILState_Ensure();
    // Make the call into cython that will call the python callable
    LOG(WARNING) << "GREG: Making call into cython from C++\n";
    LOG(WARNING) << absl::StrFormat("GREG: sign_py_wrapper_ is nullptr: %v",
                                    self->sign_py_wrapper_ == nullptr);
    LOG(WARNING) << absl::StrFormat("GREG: sign_user_data_ is nullptr: %v",
                                    self->sign_user_data_ == nullptr);
    // auto temp = self->sign_py_wrapper_("abc", signature_algorithm, nullptr);
    auto signed_data = self->sign_py_wrapper_(data_to_sign, signature_algorithm,
                                              self->sign_user_data_);
    // We're done needing Python
    // PyGILState_Release(gstate);
    LOG(WARNING) << "GREG: Calling on_complete\n";
    on_complete(signed_data);
  });
  // Some more involved handle with event engine?
  return std::make_shared<grpc_core::AsyncSigningHandle>();
}

void PrivateKeySignerPyWrapper::Cancel(
    std::shared_ptr<AsyncSigningHandle>) { /* TODO(gregorycooke) will need to
                                              bubble up to Python? */
}

std::shared_ptr<PrivateKeySigner> BuildPrivateKeySigner(
    SignWrapperForPy sign_py_wrapper, void* user_data) {
  LOG(WARNING) << "GREG: BuildPrivateKeySigner\n";
  return std::make_shared<PrivateKeySignerPyWrapper>(sign_py_wrapper,
                                                     user_data);
}

}  // namespace grpc_core
