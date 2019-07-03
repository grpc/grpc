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
#include "src/core/lib/gprpp/ref_counted_ptr.h"
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
#define GRPC_LOCAL_TRANSPORT_SECURITY_TYPE "local"

namespace {

grpc_core::RefCountedPtr<grpc_auth_context> local_auth_context_create() {
  /* Create auth context. */
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context_add_cstring_property(
      ctx.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_LOCAL_TRANSPORT_SECURITY_TYPE);
  GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                 ctx.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME) == 1);
  return ctx;
}

void local_check_peer(grpc_security_connector* sc, tsi_peer peer,
                      grpc_endpoint* ep,
                      grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
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
  /* Create an auth context which is necessary to pass the santiy check in
   * {client, server}_auth_filter that verifies if the peer's auth context is
   * obtained during handshakes. The auth context is only checked for its
   * existence and not actually used.
   */
  *auth_context = local_auth_context_create();
  error = *auth_context != nullptr ? GRPC_ERROR_NONE
                                   : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                         "Could not create local auth context");
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
}

class grpc_local_channel_security_connector final
    : public grpc_channel_security_connector {
 public:
  grpc_local_channel_security_connector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name)
      : grpc_channel_security_connector(nullptr, std::move(channel_creds),
                                        std::move(request_metadata_creds)),
        target_name_(gpr_strdup(target_name)) {}

  ~grpc_local_channel_security_connector() override { gpr_free(target_name_); }

  void add_handshakers(
      grpc_pollset_set* interested_parties,
      grpc_core::HandshakeManager* handshake_manager) override {
    tsi_handshaker* handshaker = nullptr;
    GPR_ASSERT(local_tsi_handshaker_create(true /* is_client */, &handshaker) ==
               TSI_OK);
    handshake_manager->Add(
        grpc_core::SecurityHandshakerCreate(handshaker, this));
  }

  int cmp(const grpc_security_connector* other_sc) const override {
    auto* other =
        reinterpret_cast<const grpc_local_channel_security_connector*>(
            other_sc);
    int c = channel_security_connector_cmp(other);
    if (c != 0) return c;
    return strcmp(target_name_, other->target_name_);
  }

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    grpc_local_credentials* creds =
        reinterpret_cast<grpc_local_credentials*>(mutable_channel_creds());
    local_check_peer(this, peer, ep, auth_context, on_peer_checked,
                     creds->connect_type());
  }

  bool check_call_host(grpc_core::StringView host,
                       grpc_auth_context* auth_context,
                       grpc_closure* on_call_host_checked,
                       grpc_error** error) override {
    if (host.empty() || host != target_name_) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "local call host does not match target name");
    }
    return true;
  }

  void cancel_check_call_host(grpc_closure* on_call_host_checked,
                              grpc_error* error) override {
    GRPC_ERROR_UNREF(error);
  }

  const char* target_name() const { return target_name_; }

 private:
  char* target_name_;
};

class grpc_local_server_security_connector final
    : public grpc_server_security_connector {
 public:
  grpc_local_server_security_connector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds)
      : grpc_server_security_connector(nullptr, std::move(server_creds)) {}
  ~grpc_local_server_security_connector() override = default;

  void add_handshakers(
      grpc_pollset_set* interested_parties,
      grpc_core::HandshakeManager* handshake_manager) override {
    tsi_handshaker* handshaker = nullptr;
    GPR_ASSERT(local_tsi_handshaker_create(false /* is_client */,
                                           &handshaker) == TSI_OK);
    handshake_manager->Add(
        grpc_core::SecurityHandshakerCreate(handshaker, this));
  }

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    grpc_local_server_credentials* creds =
        static_cast<grpc_local_server_credentials*>(mutable_server_creds());
    local_check_peer(this, peer, ep, auth_context, on_peer_checked,
                     creds->connect_type());
  }

  int cmp(const grpc_security_connector* other) const override {
    return server_security_connector_cmp(
        static_cast<const grpc_server_security_connector*>(other));
  }
};
}  // namespace

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_local_channel_security_connector_create(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const grpc_channel_args* args, const char* target_name) {
  if (channel_creds == nullptr || target_name == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to grpc_local_channel_security_connector_create()");
    return nullptr;
  }
  // Perform sanity check on UDS address. For TCP local connection, the check
  // will be done during check_peer procedure.
  grpc_local_credentials* creds =
      static_cast<grpc_local_credentials*>(channel_creds.get());
  const grpc_arg* server_uri_arg =
      grpc_channel_args_find(args, GRPC_ARG_SERVER_URI);
  const char* server_uri_str = grpc_channel_arg_get_string(server_uri_arg);
  if (creds->connect_type() == UDS &&
      strncmp(GRPC_UDS_URI_PATTERN, server_uri_str,
              strlen(GRPC_UDS_URI_PATTERN)) != 0) {
    gpr_log(GPR_ERROR,
            "Invalid UDS target name to "
            "grpc_local_channel_security_connector_create()");
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_local_channel_security_connector>(
      channel_creds, request_metadata_creds, target_name);
}

grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_local_server_security_connector_create(
    grpc_core::RefCountedPtr<grpc_server_credentials> server_creds) {
  if (server_creds == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to grpc_local_server_security_connector_create()");
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_local_server_security_connector>(
      std::move(server_creds));
}
