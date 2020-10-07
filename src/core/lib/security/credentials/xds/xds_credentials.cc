//
//
// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/xds/xds_credentials.h"

namespace grpc_core {

constexpr const char XdsCredentials::kCredentialsTypeXds[];

grpc_core::RefCountedPtr<grpc_channel_security_connector>
XdsCredentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target_name, const grpc_channel_args* args,
    grpc_channel_args** new_args) {
  /* TODO(yashkt) : To be filled */
  if (fallback_credentials_ != nullptr) {
    return fallback_credentials_->create_security_connector(
        std::move(call_creds), target_name, args, new_args);
  }
  return nullptr;
}

}  // namespace grpc_core

grpc_channel_credentials* grpc_xds_credentials_create(
    grpc_channel_credentials* fallback_credentials) {
  return new grpc_core::XdsCredentials(fallback_credentials->Ref());
}
