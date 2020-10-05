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

#include "src/core/lib/security/credentials/insecure/insecure_credentials.h"

namespace grpc_core {

namespace {

class InsecureChannelSecurityConnector : public grpc_channel_security_connector {
public:
  InsecureChannelSecurityConnector() : grpc_channel_security_connector(nullptr, nullptr, nullptr) {}

 bool check_call_host(absl::string_view host,
                               grpc_auth_context* auth_context,
                               grpc_closure* on_call_host_checked,
                               grpc_error** error) override { 
  *error = GRPC_ERROR_NONE;
  return true; 
}  

  void cancel_check_call_host(grpc_closure* on_call_host_checked,
                                      grpc_error* error) override {
    GRPC_ERROR_UNREF(error);
  }

 void add_handshakers(const grpc_channel_args* args,
                               grpc_pollset_set* interested_parties,
                               grpc_core::HandshakeManager* handshake_mgr) override {} 

  void check_peer(
      tsi_peer peer, grpc_endpoint* ep,
      grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
      grpc_closure* on_peer_checked) override{
    Closure::Run(on_peer_checked, GRPC_ERROR_NONE);
  }

  int cmp(const grpc_security_connector* other_sc) const override {
    return channel_security_connector_cmp(other);
  }
};

constexpr char kCredentialsTypeInsecure[] = "insecure";

class InsecureCredentials final : public grpc_channel_credentials {
 public:
  explicit InsecureCredentials()
      : grpc_channel_credentials(kCredentialsTypeInsecure) {}

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* target_name, const grpc_channel_args* args,
      grpc_channel_args** new_args) override;
};

grpc_core::RefCountedPtr<grpc_channel_security_connector>
InsecureCredentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target_name, const grpc_channel_args* args,
    grpc_channel_args** new_args) {
  return  MakeRefCounted<InsecureChannelSecurityConnector>();
}
} // namespace
}  // namespace grpc_core

grpc_channel_credentials* grpc_insecure_credentials_create() {
  return new grpc_core::InsecureCredentials();
}

grpc_channel_credentials* grpc_insecure_server_credentials_create() {
  return new grpc_core::InsecureServerCredentials();
}

