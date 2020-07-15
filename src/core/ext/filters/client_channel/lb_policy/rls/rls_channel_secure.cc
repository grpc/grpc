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

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

#include "src/core/ext/filters/client_channel/lb_policy/rls/rls_channel.h"
#include "src/core/lib/security/credentials/credentials.h"

namespace grpc_core {

grpc_channel* grpc_rls_channel_create(const std::string& target,
                                      const grpc_channel_args* channel_args) {
  grpc_channel_credentials* creds =
      grpc_channel_credentials_find_in_args(channel_args);
  return grpc_secure_channel_create(creds, target.c_str(), nullptr, nullptr);
}

}  // namespace grpc_core
