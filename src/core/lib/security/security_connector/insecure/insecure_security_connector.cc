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
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/tsi/local_transport_security.h"

namespace grpc_core {

const char kInsecureTransportSecurityType[] = "insecure";

namespace {

RefCountedPtr<grpc_auth_context> MakeAuthContext() {
  auto ctx = MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context_add_cstring_property(
      ctx.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      kInsecureTransportSecurityType);
  const char* security_level = tsi_security_level_to_string(TSI_SECURITY_NONE);
  grpc_auth_context_add_property(ctx.get(),
                                 GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME,
                                 security_level, strlen(security_level));
  return ctx;
}

}  // namespace

RefCountedPtr<grpc_auth_context> TestOnlyMakeInsecureAuthContext() {
  return MakeAuthContext();
}

// check_call_host and cancel_check_call_host are no-ops since we want to
// provide an insecure channel.
bool InsecureChannelSecurityConnector::check_call_host(
    absl::string_view /*host*/, grpc_auth_context* /*auth_context*/,
    grpc_closure* /*on_call_host_checked*/, grpc_error_handle* error) {
  *error = GRPC_ERROR_NONE;
  return true;
}

void InsecureChannelSecurityConnector::cancel_check_call_host(
    grpc_closure* /*on_call_host_checked*/, grpc_error_handle error) {
  GRPC_ERROR_UNREF(error);
}

// add_handshakers should have been a no-op but we need to add a minimalist
// security handshaker so that check_peer is invoked and an auth_context is
// created with the security level of TSI_SECURITY_NONE.
void InsecureChannelSecurityConnector::add_handshakers(
    const grpc_channel_args* args, grpc_pollset_set* /* interested_parties */,
    HandshakeManager* handshake_manager) {
  tsi_handshaker* handshaker = nullptr;
  // Re-use local_tsi_handshaker_create as a minimalist handshaker.
  GPR_ASSERT(tsi_local_handshaker_create(true /* is_client */, &handshaker) ==
             TSI_OK);
  handshake_manager->Add(SecurityHandshakerCreate(handshaker, this, args));
}

void InsecureChannelSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/,
    RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  *auth_context = MakeAuthContext();
  tsi_peer_destruct(&peer);
  ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, GRPC_ERROR_NONE);
}

int InsecureChannelSecurityConnector::cmp(
    const grpc_security_connector* other_sc) const {
  return channel_security_connector_cmp(
      static_cast<const grpc_channel_security_connector*>(other_sc));
}

// add_handshakers should have been a no-op but we need to add a minimalist
// security handshaker so that check_peer is invoked and an auth_context is
// created with the security level of TSI_SECURITY_NONE.
void InsecureServerSecurityConnector::add_handshakers(
    const grpc_channel_args* args, grpc_pollset_set* /* interested_parties */,
    grpc_core::HandshakeManager* handshake_manager) {
  tsi_handshaker* handshaker = nullptr;
  // Re-use local_tsi_handshaker_create as a minimalist handshaker.
  GPR_ASSERT(tsi_local_handshaker_create(false /* is_client */, &handshaker) ==
             TSI_OK);
  handshake_manager->Add(SecurityHandshakerCreate(handshaker, this, args));
}

void InsecureServerSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  *auth_context = MakeAuthContext();
  tsi_peer_destruct(&peer);
  ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, GRPC_ERROR_NONE);
}

int InsecureServerSecurityConnector::cmp(
    const grpc_security_connector* other) const {
  return server_security_connector_cmp(
      static_cast<const grpc_server_security_connector*>(other));
}

}  // namespace grpc_core
