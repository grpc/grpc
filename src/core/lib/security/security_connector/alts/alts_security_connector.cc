/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/security_connector/alts/alts_security_connector.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security.h"

void grpc_alts_set_rpc_protocol_versions(
    grpc_gcp_rpc_protocol_versions* rpc_versions) {
  grpc_gcp_rpc_protocol_versions_set_max(rpc_versions,
                                         GRPC_PROTOCOL_VERSION_MAX_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MAX_MINOR);
  grpc_gcp_rpc_protocol_versions_set_min(rpc_versions,
                                         GRPC_PROTOCOL_VERSION_MIN_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MIN_MINOR);
}

namespace {

void alts_check_peer(tsi_peer peer,
                     grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                     grpc_closure* on_peer_checked) {
  *auth_context =
      grpc_core::internal::grpc_alts_auth_context_from_tsi_peer(&peer);
  tsi_peer_destruct(&peer);
  grpc_error* error =
      *auth_context != nullptr
          ? GRPC_ERROR_NONE
          : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Could not get ALTS auth context from TSI peer");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
}

class grpc_alts_channel_security_connector final
    : public grpc_channel_security_connector {
 public:
  grpc_alts_channel_security_connector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name)
      : grpc_channel_security_connector(GRPC_ALTS_URL_SCHEME,
                                        std::move(channel_creds),
                                        std::move(request_metadata_creds)),
        target_name_(gpr_strdup(target_name)) {}

  ~grpc_alts_channel_security_connector() override { gpr_free(target_name_); }

  void add_handshakers(
      const grpc_channel_args* args, grpc_pollset_set* interested_parties,
      grpc_core::HandshakeManager* handshake_manager) override {
    tsi_handshaker* handshaker = nullptr;
    const grpc_alts_credentials* creds =
        static_cast<const grpc_alts_credentials*>(channel_creds());
    GPR_ASSERT(alts_tsi_handshaker_create(creds->options(), target_name_,
                                          creds->handshaker_service_url(), true,
                                          interested_parties,
                                          &handshaker) == TSI_OK);
    handshake_manager->Add(
        grpc_core::SecurityHandshakerCreate(handshaker, this, args));
  }

  void check_peer(tsi_peer peer, grpc_endpoint* /*ep*/,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    alts_check_peer(peer, auth_context, on_peer_checked);
  }

  int cmp(const grpc_security_connector* other_sc) const override {
    auto* other =
        reinterpret_cast<const grpc_alts_channel_security_connector*>(other_sc);
    int c = channel_security_connector_cmp(other);
    if (c != 0) return c;
    return strcmp(target_name_, other->target_name_);
  }

  bool check_call_host(grpc_core::StringView host,
                       grpc_auth_context* /*auth_context*/,
                       grpc_closure* /*on_call_host_checked*/,
                       grpc_error** error) override {
    if (host.empty() || host != target_name_) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "ALTS call host does not match target name");
    }
    return true;
  }

  void cancel_check_call_host(grpc_closure* /*on_call_host_checked*/,
                              grpc_error* error) override {
    GRPC_ERROR_UNREF(error);
  }

 private:
  char* target_name_;
};

class grpc_alts_server_security_connector final
    : public grpc_server_security_connector {
 public:
  grpc_alts_server_security_connector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds)
      : grpc_server_security_connector(GRPC_ALTS_URL_SCHEME,
                                       std::move(server_creds)) {}

  ~grpc_alts_server_security_connector() override = default;

  void add_handshakers(
      const grpc_channel_args* args, grpc_pollset_set* interested_parties,
      grpc_core::HandshakeManager* handshake_manager) override {
    tsi_handshaker* handshaker = nullptr;
    const grpc_alts_server_credentials* creds =
        static_cast<const grpc_alts_server_credentials*>(server_creds());
    GPR_ASSERT(alts_tsi_handshaker_create(
                   creds->options(), nullptr, creds->handshaker_service_url(),
                   false, interested_parties, &handshaker) == TSI_OK);
    handshake_manager->Add(
        grpc_core::SecurityHandshakerCreate(handshaker, this, args));
  }

  void check_peer(tsi_peer peer, grpc_endpoint* /*ep*/,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    alts_check_peer(peer, auth_context, on_peer_checked);
  }

  int cmp(const grpc_security_connector* other) const override {
    return server_security_connector_cmp(
        static_cast<const grpc_server_security_connector*>(other));
  }
};
}  // namespace

namespace grpc_core {
namespace internal {
grpc_core::RefCountedPtr<grpc_auth_context>
grpc_alts_auth_context_from_tsi_peer(const tsi_peer* peer) {
  if (peer == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to grpc_alts_auth_context_from_tsi_peer()");
    return nullptr;
  }
  /* Validate certificate type. */
  const tsi_peer_property* cert_type_prop =
      tsi_peer_get_property_by_name(peer, TSI_CERTIFICATE_TYPE_PEER_PROPERTY);
  if (cert_type_prop == nullptr ||
      strncmp(cert_type_prop->value.data, TSI_ALTS_CERTIFICATE_TYPE,
              cert_type_prop->value.length) != 0) {
    gpr_log(GPR_ERROR, "Invalid or missing certificate type property.");
    return nullptr;
  }
  /* Check if security level exists. */
  const tsi_peer_property* security_level_prop =
      tsi_peer_get_property_by_name(peer, TSI_SECURITY_LEVEL_PEER_PROPERTY);
  if (security_level_prop == nullptr) {
    gpr_log(GPR_ERROR, "Missing security level property.");
    return nullptr;
  }
  /* Validate RPC protocol versions. */
  const tsi_peer_property* rpc_versions_prop =
      tsi_peer_get_property_by_name(peer, TSI_ALTS_RPC_VERSIONS);
  if (rpc_versions_prop == nullptr) {
    gpr_log(GPR_ERROR, "Missing rpc protocol versions property.");
    return nullptr;
  }
  grpc_gcp_rpc_protocol_versions local_versions, peer_versions;
  grpc_alts_set_rpc_protocol_versions(&local_versions);
  grpc_slice slice = grpc_slice_from_copied_buffer(
      rpc_versions_prop->value.data, rpc_versions_prop->value.length);
  bool decode_result =
      grpc_gcp_rpc_protocol_versions_decode(slice, &peer_versions);
  grpc_slice_unref_internal(slice);
  if (!decode_result) {
    gpr_log(GPR_ERROR, "Invalid peer rpc protocol versions.");
    return nullptr;
  }
  /* TODO: Pass highest common rpc protocol version to grpc caller. */
  bool check_result = grpc_gcp_rpc_protocol_versions_check(
      &local_versions, &peer_versions, nullptr);
  if (!check_result) {
    gpr_log(GPR_ERROR, "Mismatch of local and peer rpc protocol versions.");
    return nullptr;
  }
  /* Validate ALTS Context. */
  const tsi_peer_property* alts_context_prop =
      tsi_peer_get_property_by_name(peer, TSI_ALTS_CONTEXT);
  if (alts_context_prop == nullptr) {
    gpr_log(GPR_ERROR, "Missing alts context property.");
    return nullptr;
  }
  /* Create auth context. */
  auto ctx = grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context_add_cstring_property(
      ctx.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_ALTS_TRANSPORT_SECURITY_TYPE);
  size_t i = 0;
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* tsi_prop = &peer->properties[i];
    /* Add service account to auth context. */
    if (strcmp(tsi_prop->name, TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(
          ctx.get(), TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY,
          tsi_prop->value.data, tsi_prop->value.length);
      GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                     ctx.get(), TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY) == 1);
    }
    /* Add alts context to auth context. */
    if (strcmp(tsi_prop->name, TSI_ALTS_CONTEXT) == 0) {
      grpc_auth_context_add_property(ctx.get(), TSI_ALTS_CONTEXT,
                                     tsi_prop->value.data,
                                     tsi_prop->value.length);
    }
    /* Add security level to auth context. */
    if (strcmp(tsi_prop->name, TSI_SECURITY_LEVEL_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(
          ctx.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME,
          tsi_prop->value.data, tsi_prop->value.length);
    }
  }
  if (!grpc_auth_context_peer_is_authenticated(ctx.get())) {
    gpr_log(GPR_ERROR, "Invalid unauthenticated peer.");
    ctx.reset(DEBUG_LOCATION, "test");
    return nullptr;
  }
  return ctx;
}

}  // namespace internal
}  // namespace grpc_core

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_alts_channel_security_connector_create(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target_name) {
  if (channel_creds == nullptr || target_name == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to grpc_alts_channel_security_connector_create()");
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_alts_channel_security_connector>(
      std::move(channel_creds), std::move(request_metadata_creds), target_name);
}

grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_alts_server_security_connector_create(
    grpc_core::RefCountedPtr<grpc_server_credentials> server_creds) {
  if (server_creds == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to grpc_alts_server_security_connector_create()");
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_alts_server_security_connector>(
      std::move(server_creds));
}
