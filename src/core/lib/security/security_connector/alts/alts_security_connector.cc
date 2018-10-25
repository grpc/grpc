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

#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security.h"

typedef struct {
  grpc_channel_security_connector base;
  char* target_name;
} grpc_alts_channel_security_connector;

typedef struct {
  grpc_server_security_connector base;
} grpc_alts_server_security_connector;

static void alts_channel_destroy(grpc_security_connector* sc) {
  if (sc == nullptr) {
    return;
  }
  auto c = reinterpret_cast<grpc_alts_channel_security_connector*>(sc);
  grpc_call_credentials_unref(c->base.request_metadata_creds);
  grpc_channel_credentials_unref(c->base.channel_creds);
  gpr_free(c->target_name);
  gpr_free(sc);
}

static void alts_server_destroy(grpc_security_connector* sc) {
  if (sc == nullptr) {
    return;
  }
  auto c = reinterpret_cast<grpc_alts_server_security_connector*>(sc);
  grpc_server_credentials_unref(c->base.server_creds);
  gpr_free(sc);
}

static void alts_channel_add_handshakers(
    grpc_channel_security_connector* sc, grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_manager) {
  tsi_handshaker* handshaker = nullptr;
  auto c = reinterpret_cast<grpc_alts_channel_security_connector*>(sc);
  grpc_alts_credentials* creds =
      reinterpret_cast<grpc_alts_credentials*>(c->base.channel_creds);
  GPR_ASSERT(alts_tsi_handshaker_create(
                 creds->options, c->target_name, creds->handshaker_service_url,
                 true, interested_parties, &handshaker) == TSI_OK);
  grpc_handshake_manager_add(handshake_manager, grpc_security_handshaker_create(
                                                    handshaker, &sc->base));
}

static void alts_server_add_handshakers(
    grpc_server_security_connector* sc, grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_manager) {
  tsi_handshaker* handshaker = nullptr;
  auto c = reinterpret_cast<grpc_alts_server_security_connector*>(sc);
  grpc_alts_server_credentials* creds =
      reinterpret_cast<grpc_alts_server_credentials*>(c->base.server_creds);
  GPR_ASSERT(alts_tsi_handshaker_create(
                 creds->options, nullptr, creds->handshaker_service_url, false,
                 interested_parties, &handshaker) == TSI_OK);
  grpc_handshake_manager_add(handshake_manager, grpc_security_handshaker_create(
                                                    handshaker, &sc->base));
}

static void alts_set_rpc_protocol_versions(
    grpc_gcp_rpc_protocol_versions* rpc_versions) {
  grpc_gcp_rpc_protocol_versions_set_max(rpc_versions,
                                         GRPC_PROTOCOL_VERSION_MAX_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MAX_MINOR);
  grpc_gcp_rpc_protocol_versions_set_min(rpc_versions,
                                         GRPC_PROTOCOL_VERSION_MIN_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MIN_MINOR);
}

namespace grpc_core {
namespace internal {

grpc_security_status grpc_alts_auth_context_from_tsi_peer(
    const tsi_peer* peer, grpc_auth_context** ctx) {
  if (peer == nullptr || ctx == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to grpc_alts_auth_context_from_tsi_peer()");
    return GRPC_SECURITY_ERROR;
  }
  *ctx = nullptr;
  /* Validate certificate type. */
  const tsi_peer_property* cert_type_prop =
      tsi_peer_get_property_by_name(peer, TSI_CERTIFICATE_TYPE_PEER_PROPERTY);
  if (cert_type_prop == nullptr ||
      strncmp(cert_type_prop->value.data, TSI_ALTS_CERTIFICATE_TYPE,
              cert_type_prop->value.length) != 0) {
    gpr_log(GPR_ERROR, "Invalid or missing certificate type property.");
    return GRPC_SECURITY_ERROR;
  }
  /* Validate RPC protocol versions. */
  const tsi_peer_property* rpc_versions_prop =
      tsi_peer_get_property_by_name(peer, TSI_ALTS_RPC_VERSIONS);
  if (rpc_versions_prop == nullptr) {
    gpr_log(GPR_ERROR, "Missing rpc protocol versions property.");
    return GRPC_SECURITY_ERROR;
  }
  grpc_gcp_rpc_protocol_versions local_versions, peer_versions;
  alts_set_rpc_protocol_versions(&local_versions);
  grpc_slice slice = grpc_slice_from_copied_buffer(
      rpc_versions_prop->value.data, rpc_versions_prop->value.length);
  bool decode_result =
      grpc_gcp_rpc_protocol_versions_decode(slice, &peer_versions);
  grpc_slice_unref_internal(slice);
  if (!decode_result) {
    gpr_log(GPR_ERROR, "Invalid peer rpc protocol versions.");
    return GRPC_SECURITY_ERROR;
  }
  /* TODO: Pass highest common rpc protocol version to grpc caller. */
  bool check_result = grpc_gcp_rpc_protocol_versions_check(
      &local_versions, &peer_versions, nullptr);
  if (!check_result) {
    gpr_log(GPR_ERROR, "Mismatch of local and peer rpc protocol versions.");
    return GRPC_SECURITY_ERROR;
  }
  /* Create auth context. */
  *ctx = grpc_auth_context_create(nullptr);
  grpc_auth_context_add_cstring_property(
      *ctx, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_ALTS_TRANSPORT_SECURITY_TYPE);
  size_t i = 0;
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* tsi_prop = &peer->properties[i];
    /* Add service account to auth context. */
    if (strcmp(tsi_prop->name, TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(
          *ctx, TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, tsi_prop->value.data,
          tsi_prop->value.length);
      GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                     *ctx, TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY) == 1);
    }
  }
  if (!grpc_auth_context_peer_is_authenticated(*ctx)) {
    gpr_log(GPR_ERROR, "Invalid unauthenticated peer.");
    GRPC_AUTH_CONTEXT_UNREF(*ctx, "test");
    *ctx = nullptr;
    return GRPC_SECURITY_ERROR;
  }
  return GRPC_SECURITY_OK;
}

}  // namespace internal
}  // namespace grpc_core

static void alts_check_peer(grpc_security_connector* sc, tsi_peer peer,
                            grpc_auth_context** auth_context,
                            grpc_closure* on_peer_checked) {
  grpc_security_status status;
  status = grpc_core::internal::grpc_alts_auth_context_from_tsi_peer(
      &peer, auth_context);
  tsi_peer_destruct(&peer);
  grpc_error* error =
      status == GRPC_SECURITY_OK
          ? GRPC_ERROR_NONE
          : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Could not get ALTS auth context from TSI peer");
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
}

static int alts_channel_cmp(grpc_security_connector* sc1,
                            grpc_security_connector* sc2) {
  grpc_alts_channel_security_connector* c1 =
      reinterpret_cast<grpc_alts_channel_security_connector*>(sc1);
  grpc_alts_channel_security_connector* c2 =
      reinterpret_cast<grpc_alts_channel_security_connector*>(sc2);
  int c = grpc_channel_security_connector_cmp(&c1->base, &c2->base);
  if (c != 0) return c;
  return strcmp(c1->target_name, c2->target_name);
}

static int alts_server_cmp(grpc_security_connector* sc1,
                           grpc_security_connector* sc2) {
  grpc_alts_server_security_connector* c1 =
      reinterpret_cast<grpc_alts_server_security_connector*>(sc1);
  grpc_alts_server_security_connector* c2 =
      reinterpret_cast<grpc_alts_server_security_connector*>(sc2);
  return grpc_server_security_connector_cmp(&c1->base, &c2->base);
}

static grpc_security_connector_vtable alts_channel_vtable = {
    alts_channel_destroy, alts_check_peer, alts_channel_cmp};

static grpc_security_connector_vtable alts_server_vtable = {
    alts_server_destroy, alts_check_peer, alts_server_cmp};

static bool alts_check_call_host(grpc_channel_security_connector* sc,
                                 const char* host,
                                 grpc_auth_context* auth_context,
                                 grpc_closure* on_call_host_checked,
                                 grpc_error** error) {
  grpc_alts_channel_security_connector* alts_sc =
      reinterpret_cast<grpc_alts_channel_security_connector*>(sc);
  if (host == nullptr || alts_sc == nullptr ||
      strcmp(host, alts_sc->target_name) != 0) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "ALTS call host does not match target name");
  }
  return true;
}

static void alts_cancel_check_call_host(grpc_channel_security_connector* sc,
                                        grpc_closure* on_call_host_checked,
                                        grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

grpc_security_status grpc_alts_channel_security_connector_create(
    grpc_channel_credentials* channel_creds,
    grpc_call_credentials* request_metadata_creds, const char* target_name,
    grpc_channel_security_connector** sc) {
  if (channel_creds == nullptr || sc == nullptr || target_name == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to grpc_alts_channel_security_connector_create()");
    return GRPC_SECURITY_ERROR;
  }
  auto c = static_cast<grpc_alts_channel_security_connector*>(
      gpr_zalloc(sizeof(grpc_alts_channel_security_connector)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &alts_channel_vtable;
  c->base.add_handshakers = alts_channel_add_handshakers;
  c->base.channel_creds = grpc_channel_credentials_ref(channel_creds);
  c->base.request_metadata_creds =
      grpc_call_credentials_ref(request_metadata_creds);
  c->base.check_call_host = alts_check_call_host;
  c->base.cancel_check_call_host = alts_cancel_check_call_host;
  grpc_alts_credentials* creds =
      reinterpret_cast<grpc_alts_credentials*>(c->base.channel_creds);
  alts_set_rpc_protocol_versions(&creds->options->rpc_versions);
  c->target_name = gpr_strdup(target_name);
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}

grpc_security_status grpc_alts_server_security_connector_create(
    grpc_server_credentials* server_creds,
    grpc_server_security_connector** sc) {
  if (server_creds == nullptr || sc == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to grpc_alts_server_security_connector_create()");
    return GRPC_SECURITY_ERROR;
  }
  auto c = static_cast<grpc_alts_server_security_connector*>(
      gpr_zalloc(sizeof(grpc_alts_server_security_connector)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &alts_server_vtable;
  c->base.server_creds = grpc_server_credentials_ref(server_creds);
  c->base.add_handshakers = alts_server_add_handshakers;
  grpc_alts_server_credentials* creds =
      reinterpret_cast<grpc_alts_server_credentials*>(c->base.server_creds);
  alts_set_rpc_protocol_versions(&creds->options->rpc_versions);
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}
