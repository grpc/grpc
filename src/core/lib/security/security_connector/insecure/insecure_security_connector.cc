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

#include "src/core/lib/security/security_connector/insecure/insecure_security_connector.h"

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/tsi/local_transport_security.h"

namespace grpc_core {
namespace {

constexpr char kInsecureTransportSecurityType[] = "insecure";

void insecure_check_peer(
    tsi_peer peer, grpc_endpoint* /* ep */,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  grpc_error* error = GRPC_ERROR_NONE;
  // Create an auth context which is necessary to pass the santiy check in
  // {client, server}_auth_filter that verifies if the peer's auth context is
  // obtained during handshakes. The auth context is only checked for its
  // existence and not actually used.
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context_add_cstring_property(
      ctx.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      kInsecureTransportSecurityType);
  GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                 ctx.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME) == 1);
  const char* security_level = tsi_security_level_to_string(TSI_SECURITY_NONE);
  grpc_auth_context_add_property(ctx.get(),
                                 GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME,
                                 security_level, strlen(security_level));
  *auth_context = std::move(ctx);
  tsi_peer_destruct(&peer);
  ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
}

class InsecureChannelSecurityConnector
    : public grpc_channel_security_connector {
 public:
  InsecureChannelSecurityConnector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds)
      : grpc_channel_security_connector(/* url_scheme */ nullptr,
                                        std::move(channel_creds),
                                        std::move(request_metadata_creds)) {}

  // check_call_host and cancel_check_call_host are no-ops since we want to
  // provide an insecure channel.
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

  // add_handshakers should have been a no-op but we need to add a minimalist
  // security handshaker so that check_peer is invoked and an auth_context can
  // be created with the security level of TSI_SECURITY_NONE.
  void add_handshakers(
      const grpc_channel_args* args, grpc_pollset_set* /* interested_parties */,
      grpc_core::HandshakeManager* handshake_manager) override {
    tsi_handshaker* handshaker = nullptr;
    // Re-use local_tsi_handshaker_create as a minimalist handshaker.
    GPR_ASSERT(local_tsi_handshaker_create(true /* is_client */, &handshaker) ==
               TSI_OK);
    handshake_manager->Add(SecurityHandshakerCreate(handshaker, this, args));
  }

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    insecure_check_peer(peer, ep, auth_context, on_peer_checked);
  }

  int cmp(const grpc_security_connector* other_sc) const override {
    return channel_security_connector_cmp(
        static_cast<const grpc_channel_security_connector*>(other_sc));
  }
};

}  // namespace

RefCountedPtr<grpc_channel_security_connector>
InsecureChannelSecurityConnectorCreate(
    RefCountedPtr<grpc_channel_credentials> channel_creds,
    RefCountedPtr<grpc_call_credentials> request_metadata_creds) {
  return MakeRefCounted<InsecureChannelSecurityConnector>(
      std::move(channel_creds), std::move(request_metadata_creds));
}

}  // namespace grpc_core
