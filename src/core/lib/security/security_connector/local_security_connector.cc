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

#include "src/core/lib/security/security_connector/local_security_connector.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/local/local_credentials.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/tsi/local_transport_security.h"

#define GRPC_UDS_URI_PATTERN "unix:"
#define GRPC_UDS_URL_SCHEME "unix"
#define GRPC_LOCAL_TRANSPORT_SECURITY_TYPE "local"

typedef struct {
  grpc_channel_security_connector base;
  char* target_name;
} grpc_local_channel_security_connector;

typedef struct {
  grpc_server_security_connector base;
} grpc_local_server_security_connector;

static void local_channel_destroy(grpc_security_connector* sc) {
  if (sc == nullptr) {
    return;
  }
  auto c = reinterpret_cast<grpc_local_channel_security_connector*>(sc);
  grpc_call_credentials_unref(c->base.request_metadata_creds);
  grpc_channel_credentials_unref(c->base.channel_creds);
  gpr_free(c->target_name);
  gpr_free(sc);
}

static void local_server_destroy(grpc_security_connector* sc) {
  if (sc == nullptr) {
    return;
  }
  auto c = reinterpret_cast<grpc_local_server_security_connector*>(sc);
  grpc_server_credentials_unref(c->base.server_creds);
  gpr_free(sc);
}

static void local_channel_add_handshakers(
    grpc_channel_security_connector* sc,
    grpc_handshake_manager* handshake_manager) {
  tsi_handshaker* handshaker = nullptr;
  GPR_ASSERT(local_tsi_handshaker_create(true /* is_client */, &handshaker) ==
             TSI_OK);
  grpc_handshake_manager_add(handshake_manager, grpc_security_handshaker_create(
                                                    handshaker, &sc->base));
}

static void local_server_add_handshakers(
    grpc_server_security_connector* sc,
    grpc_handshake_manager* handshake_manager) {
  tsi_handshaker* handshaker = nullptr;
  GPR_ASSERT(local_tsi_handshaker_create(false /* is_client */, &handshaker) ==
             TSI_OK);
  grpc_handshake_manager_add(handshake_manager, grpc_security_handshaker_create(
                                                    handshaker, &sc->base));
}

static int local_channel_cmp(grpc_security_connector* sc1,
                             grpc_security_connector* sc2) {
  grpc_local_channel_security_connector* c1 =
      reinterpret_cast<grpc_local_channel_security_connector*>(sc1);
  grpc_local_channel_security_connector* c2 =
      reinterpret_cast<grpc_local_channel_security_connector*>(sc2);
  int c = grpc_channel_security_connector_cmp(&c1->base, &c2->base);
  if (c != 0) return c;
  return strcmp(c1->target_name, c2->target_name);
}

static int local_server_cmp(grpc_security_connector* sc1,
                            grpc_security_connector* sc2) {
  grpc_local_server_security_connector* c1 =
      reinterpret_cast<grpc_local_server_security_connector*>(sc1);
  grpc_local_server_security_connector* c2 =
      reinterpret_cast<grpc_local_server_security_connector*>(sc2);
  return grpc_server_security_connector_cmp(&c1->base, &c2->base);
}

static grpc_security_status local_auth_context_create(grpc_auth_context** ctx) {
  if (ctx == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to local_auth_context_create()");
    return GRPC_SECURITY_ERROR;
  }
  /* Create auth context. */
  *ctx = grpc_auth_context_create(nullptr);
  grpc_auth_context_add_cstring_property(
      *ctx, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_LOCAL_TRANSPORT_SECURITY_TYPE);
  GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                 *ctx, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME) == 1);
  return GRPC_SECURITY_OK;
}

static void local_check_peer(grpc_security_connector* sc, tsi_peer peer,
                             grpc_auth_context** auth_context,
                             grpc_closure* on_peer_checked) {
  grpc_security_status status;
  /* Create an auth context which is necessary to pass the santiy check in
   * {client, server}_auth_filter that verifies if the peer's auth context is
   * obtained during handshakes. The auth context is only checked for its
   * existence and not actually used.
   */
  status = local_auth_context_create(auth_context);
  grpc_error* error = status == GRPC_SECURITY_OK
                          ? GRPC_ERROR_NONE
                          : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                "Could not create local auth context");
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
}

static grpc_security_connector_vtable local_channel_vtable = {
    local_channel_destroy, local_check_peer, local_channel_cmp};

static grpc_security_connector_vtable local_server_vtable = {
    local_server_destroy, local_check_peer, local_server_cmp};

static bool local_check_call_host(grpc_channel_security_connector* sc,
                                  const char* host,
                                  grpc_auth_context* auth_context,
                                  grpc_closure* on_call_host_checked,
                                  grpc_error** error) {
  grpc_local_channel_security_connector* local_sc =
      reinterpret_cast<grpc_local_channel_security_connector*>(sc);
  if (host == nullptr || local_sc == nullptr ||
      strcmp(host, local_sc->target_name) != 0) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "local call host does not match target name");
  }
  return true;
}

static void local_cancel_check_call_host(grpc_channel_security_connector* sc,
                                         grpc_closure* on_call_host_checked,
                                         grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

grpc_security_status grpc_local_channel_security_connector_create(
    grpc_channel_credentials* channel_creds,
    grpc_call_credentials* request_metadata_creds,
    const grpc_channel_args* args, const char* target_name,
    grpc_channel_security_connector** sc) {
  if (channel_creds == nullptr || sc == nullptr || target_name == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to grpc_local_channel_security_connector_create()");
    return GRPC_SECURITY_ERROR;
  }
  // Check if local_connect_type is UDS. Only UDS is supported for now.
  grpc_local_credentials* creds =
      reinterpret_cast<grpc_local_credentials*>(channel_creds);
  if (creds->connect_type != UDS) {
    gpr_log(GPR_ERROR,
            "Invalid local channel type to "
            "grpc_local_channel_security_connector_create()");
    return GRPC_SECURITY_ERROR;
  }
  // Check if target_name is a valid UDS address.
  const grpc_arg* server_uri_arg =
      grpc_channel_args_find(args, GRPC_ARG_SERVER_URI);
  const char* server_uri_str = grpc_channel_arg_get_string(server_uri_arg);
  if (strncmp(GRPC_UDS_URI_PATTERN, server_uri_str,
              strlen(GRPC_UDS_URI_PATTERN)) != 0) {
    gpr_log(GPR_ERROR,
            "Invalid target_name to "
            "grpc_local_channel_security_connector_create()");
    return GRPC_SECURITY_ERROR;
  }
  auto c = static_cast<grpc_local_channel_security_connector*>(
      gpr_zalloc(sizeof(grpc_local_channel_security_connector)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &local_channel_vtable;
  c->base.add_handshakers = local_channel_add_handshakers;
  c->base.channel_creds = grpc_channel_credentials_ref(channel_creds);
  c->base.request_metadata_creds =
      grpc_call_credentials_ref(request_metadata_creds);
  c->base.check_call_host = local_check_call_host;
  c->base.cancel_check_call_host = local_cancel_check_call_host;
  c->base.base.url_scheme =
      creds->connect_type == UDS ? GRPC_UDS_URL_SCHEME : nullptr;
  c->target_name = gpr_strdup(target_name);
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}

grpc_security_status grpc_local_server_security_connector_create(
    grpc_server_credentials* server_creds,
    grpc_server_security_connector** sc) {
  if (server_creds == nullptr || sc == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to grpc_local_server_security_connector_create()");
    return GRPC_SECURITY_ERROR;
  }
  // Check if local_connect_type is UDS. Only UDS is supported for now.
  grpc_local_server_credentials* creds =
      reinterpret_cast<grpc_local_server_credentials*>(server_creds);
  if (creds->connect_type != UDS) {
    gpr_log(GPR_ERROR,
            "Invalid local server type to "
            "grpc_local_server_security_connector_create()");
    return GRPC_SECURITY_ERROR;
  }
  auto c = static_cast<grpc_local_server_security_connector*>(
      gpr_zalloc(sizeof(grpc_local_server_security_connector)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &local_server_vtable;
  c->base.server_creds = grpc_server_credentials_ref(server_creds);
  c->base.base.url_scheme =
      creds->connect_type == UDS ? GRPC_UDS_URL_SCHEME : nullptr;
  c->base.add_handshakers = local_server_add_handshakers;
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}
