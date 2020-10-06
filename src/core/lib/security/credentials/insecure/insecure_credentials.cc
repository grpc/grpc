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

#include <grpc/grpc_security.h>

#include "src/core/lib/security/credentials/credentials.h"

namespace grpc_core {
namespace {

class InsecureChannelSecurityConnector
    : public grpc_channel_security_connector {
 public:
  InsecureChannelSecurityConnector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds)
      : grpc_channel_security_connector(/* url_scheme */ nullptr,
                                        std::move(channel_creds),
                                        std::move(request_metadata_creds)) {}

  // check_call_host and cancel_check_call_host are not used right now, but
  // these will become useful once we make grpc_insecure_credentials_create()
  // the default method of creating an insecure channel. Currently
  // grpc_insecure_channel_create() is used to create insecure channel and this
  // method does not allow any dependencies on channel_credentials. Once we
  // remove insecure builds from gRPC, we will be able to use this.
  bool check_call_host(absl::string_view host, grpc_auth_context* auth_context,
                       grpc_closure* on_call_host_checked,
                       grpc_error** error) override {
    *error = GRPC_ERROR_NONE;
    return true;
  }

  void cancel_check_call_host(grpc_closure* on_call_host_checked,
                              grpc_error* error) override {
    GRPC_ERROR_UNREF(error);
  }

  // This is a no-op. No handshakers are added.
  void add_handshakers(
      const grpc_channel_args* /* args */,
      grpc_pollset_set* /* interested_parties */,
      grpc_core::HandshakeManager* /* handshake_mgr */) override {}

  // check_peer is needed by handshakers which aren't being added, and so this
  // should not be called but follow the method contract anway and destroy \a
  // peer and invoke \a on_peer_checked.
  void check_peer(
      tsi_peer peer, grpc_endpoint* /* ep */,
      grpc_core::RefCountedPtr<grpc_auth_context>* /* auth_context */,
      grpc_closure* on_peer_checked) override {
    tsi_peer_destruct(&peer);
    Closure::Run(DEBUG_LOCATION, on_peer_checked, GRPC_ERROR_NONE);
  }

  int cmp(const grpc_security_connector* other_sc) const override {
    return channel_security_connector_cmp(
        static_cast<const grpc_channel_security_connector*>(other_sc));
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
      grpc_channel_args** new_args) override {
    return MakeRefCounted<InsecureChannelSecurityConnector>(
        Ref(), std::move(call_creds));
  }
};

}  // namespace
}  // namespace grpc_core

grpc_channel_credentials* grpc_insecure_credentials_create() {
  return new grpc_core::InsecureCredentials();
}
