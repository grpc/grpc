// Copyright 2024 The gRPC Authors
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

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/security/credentials.h>

#include "src/core/lib/security/credentials/credentials.h"

namespace grpc {

CallCredentials::CallCredentials(grpc_call_credentials* c_creds)
    : c_creds_(c_creds) {
  CHECK_NE(c_creds, nullptr);
}

CallCredentials::~CallCredentials() { grpc_call_credentials_release(c_creds_); }

grpc::string CallCredentials::DebugString() {
  return absl::StrCat("CallCredentials{", c_creds_->debug_string(), "}");
}

bool CallCredentials::ApplyToCall(grpc_call* call) {
  return grpc_call_set_credentials(call, c_creds_) == GRPC_CALL_OK;
}

}  // namespace grpc
