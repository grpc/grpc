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

#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/insecure/insecure_security_connector.h"

namespace grpc_core {
namespace {

class InsecureCredentials final : public grpc_channel_credentials {
 public:
  InsecureCredentials()
      : grpc_channel_credentials(GRPC_CREDENTIALS_TYPE_INSECURE) {}

  RefCountedPtr<grpc_channel_security_connector> create_security_connector(
      RefCountedPtr<grpc_call_credentials> call_creds,
      const char* /* target_name */, const grpc_channel_args* /* args */,
      grpc_channel_args** /* new_args */) override {
    return MakeRefCounted<InsecureChannelSecurityConnector>(
        Ref(), std::move(call_creds));
  }

 private:
  int cmp_impl(const grpc_channel_credentials* /* other */) const override {
    // All insecure credentials objects should compare equal.
    return 0;
  }
};

class InsecureServerCredentials final : public grpc_server_credentials {
 public:
  InsecureServerCredentials()
      : grpc_server_credentials(GRPC_CREDENTIALS_TYPE_INSECURE) {}

  RefCountedPtr<grpc_server_security_connector> create_security_connector(
      const grpc_channel_args* /* args */) override {
    return MakeRefCounted<InsecureServerSecurityConnector>(Ref());
  }
};

}  // namespace
}  // namespace grpc_core

grpc_channel_credentials* grpc_insecure_credentials_create() {
  // Create a singleton object for InsecureCredentials so that channels to the
  // same target with InsecureCredentials can reuse the subchannels.
  static auto* creds = new grpc_core::InsecureCredentials();
  return creds->Ref().release();
}

grpc_server_credentials* grpc_insecure_server_credentials_create() {
  return new grpc_core::InsecureServerCredentials();
}
