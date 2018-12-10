/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/security/security_connector/ssl_utils.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/tsi/ssl/load_system_roots.h"
#include "src/core/tsi/ssl/ssl_transport_security.h"

grpc_error* grpc_ssl_check_peer(grpc_security_connector* sc,
                                const char* peer_name, const tsi_peer* peer,
                                grpc_auth_context** auth_context) {
#if TSI_OPENSSL_ALPN_SUPPORT
  /* Check the ALPN if ALPN is supported. */
  const tsi_peer_property* p =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (p == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Cannot check peer: missing selected ALPN property.");
  }
  if (!grpc_chttp2_is_alpn_version_supported(p->value.data, p->value.length)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Cannot check peer: invalid ALPN value.");
  }
#endif /* TSI_OPENSSL_ALPN_SUPPORT */
  /* Check the peer name if specified. */
  if (peer_name != nullptr && !grpc_ssl_host_matches_name(peer, peer_name)) {
    char* msg;
    gpr_asprintf(&msg, "Peer name %s is not in peer certificate", peer_name);
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return error;
  }
  *auth_context = grpc_ssl_peer_to_auth_context(peer);
  return GRPC_ERROR_NONE;
}

int grpc_ssl_host_matches_name(const tsi_peer* peer, const char* peer_name) {
  char* allocated_name = nullptr;
  int r;

  char* ignored_port;
  gpr_split_host_port(peer_name, &allocated_name, &ignored_port);
  gpr_free(ignored_port);
  peer_name = allocated_name;
  if (!peer_name) return 0;

  // IPv6 zone-id should not be included in comparisons.
  char* const zone_id = strchr(allocated_name, '%');
  if (zone_id != nullptr) *zone_id = '\0';

  r = tsi_ssl_peer_matches_name(peer, peer_name);
  gpr_free(allocated_name);
  return r;
}

grpc_error* grpc_spiffe_check_peer_property(const tsi_peer* peer) {
  GPR_ASSERT(peer != nullptr);
  int prop_num = 0;
  for (size_t i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* prop = &peer->properties[i];
    if (prop->name == nullptr) continue;
    if (strcmp(prop->name,
               TSI_X509_SUBJECT_ALTERNATIVE_NAME_URI_PEER_PROPERTY) == 0) {
      prop_num++;
    }
  }
  /* a SPIFFE SSL certificate should have a single SAN URI extension. */
  if (prop_num != 1) {
    char* msg;
    gpr_asprintf(&msg,
                 "peer certificate should contain a single SAN URI extension.");
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return error;
  }
  return GRPC_ERROR_NONE;
}

grpc_auth_context* grpc_ssl_peer_to_auth_context(const tsi_peer* peer) {
  size_t i;
  grpc_auth_context* ctx = nullptr;
  const char* peer_identity_property_name = nullptr;

  /* The caller has checked the certificate type property. */
  GPR_ASSERT(peer->property_count >= 1);
  ctx = grpc_auth_context_create(nullptr);
  grpc_auth_context_add_cstring_property(
      ctx, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* prop = &peer->properties[i];
    if (prop->name == nullptr) continue;
    if (strcmp(prop->name, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) == 0) {
      /* If there is no subject alt name, have the CN as the identity. */
      if (peer_identity_property_name == nullptr) {
        peer_identity_property_name = GRPC_X509_CN_PROPERTY_NAME;
      }
      grpc_auth_context_add_property(ctx, GRPC_X509_CN_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name,
                      TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) == 0) {
      peer_identity_property_name = GRPC_X509_SAN_PROPERTY_NAME;
      grpc_auth_context_add_property(ctx, GRPC_X509_SAN_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_PEM_CERT_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx, GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_SSL_SESSION_REUSED_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx, GRPC_SSL_SESSION_REUSED_PROPERTY,
                                     prop->value.data, prop->value.length);
    }
  }
  if (peer_identity_property_name != nullptr) {
    GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                   ctx, peer_identity_property_name) == 1);
  }
  return ctx;
}

static void add_shallow_auth_property_to_peer(tsi_peer* peer,
                                              const grpc_auth_property* prop,
                                              const char* tsi_prop_name) {
  tsi_peer_property* tsi_prop = &peer->properties[peer->property_count++];
  tsi_prop->name = const_cast<char*>(tsi_prop_name);
  tsi_prop->value.data = prop->value;
  tsi_prop->value.length = prop->value_length;
}

tsi_peer grpc_shallow_peer_from_ssl_auth_context(
    const grpc_auth_context* auth_context) {
  size_t max_num_props = 0;
  grpc_auth_property_iterator it;
  const grpc_auth_property* prop;
  tsi_peer peer;
  memset(&peer, 0, sizeof(peer));

  it = grpc_auth_context_property_iterator(auth_context);
  while (grpc_auth_property_iterator_next(&it) != nullptr) max_num_props++;

  if (max_num_props > 0) {
    peer.properties = static_cast<tsi_peer_property*>(
        gpr_malloc(max_num_props * sizeof(tsi_peer_property)));
    it = grpc_auth_context_property_iterator(auth_context);
    while ((prop = grpc_auth_property_iterator_next(&it)) != nullptr) {
      if (strcmp(prop->name, GRPC_X509_SAN_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(
            &peer, prop, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_X509_CN_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(
            &peer, prop, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_X509_PEM_CERT_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_PEM_CERT_PROPERTY);
      }
    }
  }
  return peer;
}

void grpc_shallow_peer_destruct(tsi_peer* peer) {
  if (peer->properties != nullptr) gpr_free(peer->properties);
}

/* --- Ssl cache implementation. --- */

grpc_ssl_session_cache* grpc_ssl_session_cache_create_lru(size_t capacity) {
  tsi_ssl_session_cache* cache = tsi_ssl_session_cache_create_lru(capacity);
  return reinterpret_cast<grpc_ssl_session_cache*>(cache);
}

void grpc_ssl_session_cache_destroy(grpc_ssl_session_cache* cache) {
  tsi_ssl_session_cache* tsi_cache =
      reinterpret_cast<tsi_ssl_session_cache*>(cache);
  tsi_ssl_session_cache_unref(tsi_cache);
}

static void* grpc_ssl_session_cache_arg_copy(void* p) {
  tsi_ssl_session_cache* tsi_cache =
      reinterpret_cast<tsi_ssl_session_cache*>(p);
  // destroy call below will unref the pointer.
  tsi_ssl_session_cache_ref(tsi_cache);
  return p;
}

static void grpc_ssl_session_cache_arg_destroy(void* p) {
  tsi_ssl_session_cache* tsi_cache =
      reinterpret_cast<tsi_ssl_session_cache*>(p);
  tsi_ssl_session_cache_unref(tsi_cache);
}

static int grpc_ssl_session_cache_arg_cmp(void* p, void* q) {
  return GPR_ICMP(p, q);
}

grpc_arg grpc_ssl_session_cache_create_channel_arg(
    grpc_ssl_session_cache* cache) {
  static const grpc_arg_pointer_vtable vtable = {
      grpc_ssl_session_cache_arg_copy,
      grpc_ssl_session_cache_arg_destroy,
      grpc_ssl_session_cache_arg_cmp,
  };
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_SSL_SESSION_CACHE_ARG), cache, &vtable);
}
