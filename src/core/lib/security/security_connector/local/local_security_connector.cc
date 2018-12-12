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

#include "src/core/lib/security/security_connector/local/local_security_connector.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
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
    grpc_channel_security_connector* sc, grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_manager) {
  tsi_handshaker* handshaker = nullptr;
  GPR_ASSERT(local_tsi_handshaker_create(true /* is_client */, &handshaker) ==
             TSI_OK);
  grpc_handshake_manager_add(handshake_manager, grpc_security_handshaker_create(
                                                    handshaker, &sc->base));
}

static void local_server_add_handshakers(
    grpc_server_security_connector* sc, grpc_pollset_set* interested_parties,
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
                             grpc_endpoint* ep,
                             grpc_auth_context** auth_context,
                             grpc_closure* on_peer_checked,
                             grpc_local_connect_type type) {
  int fd = grpc_endpoint_get_fd(ep);
  grpc_resolved_address resolved_addr;
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = GRPC_MAX_SOCKADDR_SIZE;
  bool is_endpoint_local = false;
  if (getsockname(fd, reinterpret_cast<grpc_sockaddr*>(resolved_addr.addr),
                  &resolved_addr.len) == 0) {
    grpc_resolved_address addr_normalized;
    grpc_resolved_address* addr =
        grpc_sockaddr_is_v4mapped(&resolved_addr, &addr_normalized)
            ? &addr_normalized
            : &resolved_addr;
    grpc_sockaddr* sock_addr = reinterpret_cast<grpc_sockaddr*>(&addr->addr);
    // UDS
    if (type == UDS && grpc_is_unix_socket(addr)) {
      is_endpoint_local = true;
      // IPV4
    } else if (type == LOCAL_TCP && sock_addr->sa_family == GRPC_AF_INET) {
      const grpc_sockaddr_in* addr4 =
          reinterpret_cast<const grpc_sockaddr_in*>(sock_addr);
      if (grpc_htonl(addr4->sin_addr.s_addr) == INADDR_LOOPBACK) {
        is_endpoint_local = true;
      }
      // IPv6
    } else if (type == LOCAL_TCP && sock_addr->sa_family == GRPC_AF_INET6) {
      const grpc_sockaddr_in6* addr6 =
          reinterpret_cast<const grpc_sockaddr_in6*>(addr);
      if (memcmp(&addr6->sin6_addr, &in6addr_loopback,
                 sizeof(in6addr_loopback)) == 0) {
        is_endpoint_local = true;
      }
    }
  }
  grpc_error* error = GRPC_ERROR_NONE;
  if (!is_endpoint_local) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Endpoint is neither UDS or TCP loopback address.");
    GRPC_CLOSURE_SCHED(on_peer_checked, error);
    return;
  }
  grpc_security_status status;
  /* Create an auth context which is necessary to pass the santiy check in
   * {client, server}_auth_filter that verifies if the peer's auth context is
   * obtained during handshakes. The auth context is only checked for its
   * existence and not actually used.
   */
  status = local_auth_context_create(auth_context);
  error = status == GRPC_SECURITY_OK
              ? GRPC_ERROR_NONE
              : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Could not create local auth context");
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
}

static void local_channel_check_peer(grpc_security_connector* sc, tsi_peer peer,
                                     grpc_endpoint* ep,
                                     grpc_auth_context** auth_context,
                                     grpc_closure* on_peer_checked) {
  grpc_channel_security_connector* c =
      reinterpret_cast<grpc_channel_security_connector*>(sc);
  GPR_ASSERT(c != nullptr);
  GPR_ASSERT(c->channel_creds != nullptr);
  grpc_local_credentials* creds =
      reinterpret_cast<grpc_local_credentials*>(c->channel_creds);
  local_check_peer(sc, peer, ep, auth_context, on_peer_checked,
                   creds->connect_type);
}

static void local_server_check_peer(grpc_security_connector* sc, tsi_peer peer,
                                    grpc_endpoint* ep,
                                    grpc_auth_context** auth_context,
                                    grpc_closure* on_peer_checked) {
  grpc_server_security_connector* c =
      reinterpret_cast<grpc_server_security_connector*>(sc);
  GPR_ASSERT(c != nullptr);
  GPR_ASSERT(c->server_creds != nullptr);
  grpc_local_server_credentials* creds =
      reinterpret_cast<grpc_local_server_credentials*>(c->server_creds);
  local_check_peer(sc, peer, ep, auth_context, on_peer_checked,
                   creds->connect_type);
}

static grpc_security_connector_vtable local_channel_vtable = {
    local_channel_destroy, local_channel_check_peer, local_channel_cmp};

static grpc_security_connector_vtable local_server_vtable = {
    local_server_destroy, local_server_check_peer, local_server_cmp};

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
    gpr_log(GPR_ERROR,
            "Invalid arguments to grpc_local_channel_security_connector_create()");
    return GRPC_SECURITY_ERROR;
  }
  // Perform sanity check on UDS address. For TCP local connection, the check
  // will be done during check_peer procedure.
  grpc_local_credentials* creds =
      reinterpret_cast<grpc_local_credentials*>(channel_creds);
  const grpc_arg* server_uri_arg =
      grpc_channel_args_find(args, GRPC_ARG_SERVER_URI);
  const char* server_uri_str = grpc_channel_arg_get_string(server_uri_arg);
  if (creds->connect_type == UDS &&
      strncmp(GRPC_UDS_URI_PATTERN, server_uri_str,
              strlen(GRPC_UDS_URI_PATTERN)) != 0) {
    gpr_log(GPR_ERROR,
            "Invalid UDS target_name to "
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
  auto c = static_cast<grpc_local_server_security_connector*>(
      gpr_zalloc(sizeof(grpc_local_server_security_connector)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &local_server_vtable;
  c->base.server_creds = grpc_server_credentials_ref(server_creds);
  grpc_local_server_credentials* creds =
      reinterpret_cast<grpc_local_server_credentials*>(server_creds);
  c->base.base.url_scheme =
      creds->connect_type == UDS ? GRPC_UDS_URL_SCHEME : nullptr;
  c->base.add_handshakers = local_server_add_handshakers;
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}
