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

#include <vector>

#include "absl/strings/str_cat.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "src/core/tsi/ssl_transport_security.h"

/* -- Constants. -- */

#if defined(GRPC_ROOT_PEM_PATH)
static const char* installed_roots_path = GRPC_ROOT_PEM_PATH;
#elif defined(INSTALL_PREFIX)
static const char* installed_roots_path =
    INSTALL_PREFIX "/usr/share/grpc/roots.pem";
#else
static const char* installed_roots_path = "/usr/share/grpc/roots.pem";
#endif

#ifndef TSI_OPENSSL_ALPN_SUPPORT
#define TSI_OPENSSL_ALPN_SUPPORT 1
#endif

/* -- Overridden default roots. -- */

static grpc_ssl_roots_override_callback ssl_roots_override_cb = nullptr;

void grpc_set_ssl_roots_override_callback(grpc_ssl_roots_override_callback cb) {
  ssl_roots_override_cb = cb;
}

/* -- Cipher suites. -- */

static gpr_once cipher_suites_once = GPR_ONCE_INIT;
static const char* cipher_suites = nullptr;

// All cipher suites for default are compliant with HTTP2.
GPR_GLOBAL_CONFIG_DEFINE_STRING(
    grpc_ssl_cipher_suites,
    "TLS_AES_128_GCM_SHA256:"
    "TLS_AES_256_GCM_SHA384:"
    "TLS_CHACHA20_POLY1305_SHA256:"
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES256-GCM-SHA384",
    "A colon separated list of cipher suites to use with OpenSSL")

static void init_cipher_suites(void) {
  grpc_core::UniquePtr<char> value =
      GPR_GLOBAL_CONFIG_GET(grpc_ssl_cipher_suites);
  cipher_suites = value.release();
}

/* --- Util --- */

const char* grpc_get_ssl_cipher_suites(void) {
  gpr_once_init(&cipher_suites_once, init_cipher_suites);
  return cipher_suites;
}

tsi_client_certificate_request_type
grpc_get_tsi_client_certificate_request_type(
    grpc_ssl_client_certificate_request_type grpc_request_type) {
  switch (grpc_request_type) {
    case GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE:
      return TSI_DONT_REQUEST_CLIENT_CERTIFICATE;

    case GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
      return TSI_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY;

    case GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY:
      return TSI_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY;

    case GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
      return TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY;

    case GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY:
      return TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;

    default:
      return TSI_DONT_REQUEST_CLIENT_CERTIFICATE;
  }
}

tsi_tls_version grpc_get_tsi_tls_version(grpc_tls_version tls_version) {
  switch (tls_version) {
    case grpc_tls_version::TLS1_2:
      return tsi_tls_version::TSI_TLS1_2;
    case grpc_tls_version::TLS1_3:
      return tsi_tls_version::TSI_TLS1_3;
    default:
      gpr_log(GPR_INFO, "Falling back to TLS 1.2.");
      return tsi_tls_version::TSI_TLS1_2;
  }
}

grpc_error_handle grpc_ssl_check_alpn(const tsi_peer* peer) {
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
  return GRPC_ERROR_NONE;
}

grpc_error_handle grpc_ssl_check_peer_name(absl::string_view peer_name,
                                           const tsi_peer* peer) {
  /* Check the peer name if specified. */
  if (!peer_name.empty() && !grpc_ssl_host_matches_name(peer, peer_name)) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("Peer name ", peer_name, " is not in peer certificate"));
  }
  return GRPC_ERROR_NONE;
}

void grpc_tsi_ssl_pem_key_cert_pairs_destroy(tsi_ssl_pem_key_cert_pair* kp,
                                             size_t num_key_cert_pairs) {
  if (kp == nullptr) return;
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    gpr_free(const_cast<char*>(kp[i].private_key));
    gpr_free(const_cast<char*>(kp[i].cert_chain));
  }
  gpr_free(kp);
}

bool grpc_ssl_check_call_host(absl::string_view host,
                              absl::string_view target_name,
                              absl::string_view overridden_target_name,
                              grpc_auth_context* auth_context,
                              grpc_error_handle* error) {
  grpc_security_status status = GRPC_SECURITY_ERROR;
  tsi_peer peer = grpc_shallow_peer_from_ssl_auth_context(auth_context);
  if (grpc_ssl_host_matches_name(&peer, host)) status = GRPC_SECURITY_OK;
  /* If the target name was overridden, then the original target_name was
   'checked' transitively during the previous peer check at the end of the
   handshake. */
  if (!overridden_target_name.empty() && host == target_name) {
    status = GRPC_SECURITY_OK;
  }
  if (status != GRPC_SECURITY_OK) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "call host does not match SSL server name");
    gpr_log(GPR_ERROR, "call host does not match SSL server name");
  }
  grpc_shallow_peer_destruct(&peer);
  return true;
}

const char** grpc_fill_alpn_protocol_strings(size_t* num_alpn_protocols) {
  GPR_ASSERT(num_alpn_protocols != nullptr);
  *num_alpn_protocols = grpc_chttp2_num_alpn_versions();
  const char** alpn_protocol_strings = static_cast<const char**>(
      gpr_malloc(sizeof(const char*) * (*num_alpn_protocols)));
  for (size_t i = 0; i < *num_alpn_protocols; i++) {
    alpn_protocol_strings[i] = grpc_chttp2_get_alpn_version_index(i);
  }
  return alpn_protocol_strings;
}

int grpc_ssl_host_matches_name(const tsi_peer* peer,
                               absl::string_view peer_name) {
  absl::string_view allocated_name;
  absl::string_view ignored_port;
  grpc_core::SplitHostPort(peer_name, &allocated_name, &ignored_port);
  if (allocated_name.empty()) return 0;

  // IPv6 zone-id should not be included in comparisons.
  const size_t zone_id = allocated_name.find('%');
  if (zone_id != absl::string_view::npos) {
    allocated_name.remove_suffix(allocated_name.size() - zone_id);
  }
  return tsi_ssl_peer_matches_name(peer, allocated_name);
}

int grpc_ssl_cmp_target_name(absl::string_view target_name,
                             absl::string_view other_target_name,
                             absl::string_view overridden_target_name,
                             absl::string_view other_overridden_target_name) {
  int c = target_name.compare(other_target_name);
  if (c != 0) return c;
  return overridden_target_name.compare(other_overridden_target_name);
}

static bool IsSpiffeId(absl::string_view uri) {
  // Return false without logging for a non-spiffe uri scheme.
  if (!absl::StartsWith(uri, "spiffe://")) {
    return false;
  };
  if (uri.size() > 2048) {
    gpr_log(GPR_INFO, "Invalid SPIFFE ID: ID longer than 2048 bytes.");
    return false;
  }
  std::vector<absl::string_view> splits = absl::StrSplit(uri, '/');
  if (splits.size() < 4 || splits[3] == "") {
    gpr_log(GPR_INFO, "Invalid SPIFFE ID: workload id is empty.");
    return false;
  }
  if (splits[2].size() > 255) {
    gpr_log(GPR_INFO, "Invalid SPIFFE ID: domain longer than 255 characters.");
    return false;
  }
  return true;
}

grpc_core::RefCountedPtr<grpc_auth_context> grpc_ssl_peer_to_auth_context(
    const tsi_peer* peer, const char* transport_security_type) {
  size_t i;
  const char* peer_identity_property_name = nullptr;

  /* The caller has checked the certificate type property. */
  GPR_ASSERT(peer->property_count >= 1);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context_add_cstring_property(
      ctx.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      transport_security_type);
  const char* spiffe_data = nullptr;
  size_t spiffe_length = 0;
  int uri_count = 0;
  bool has_spiffe_id = false;
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* prop = &peer->properties[i];
    if (prop->name == nullptr) continue;
    if (strcmp(prop->name, TSI_X509_SUBJECT_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx.get(), GRPC_X509_SUBJECT_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) ==
               0) {
      /* If there is no subject alt name, have the CN as the identity. */
      if (peer_identity_property_name == nullptr) {
        peer_identity_property_name = GRPC_X509_CN_PROPERTY_NAME;
      }
      grpc_auth_context_add_property(ctx.get(), GRPC_X509_CN_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name,
                      TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) == 0) {
      peer_identity_property_name = GRPC_X509_SAN_PROPERTY_NAME;
      grpc_auth_context_add_property(ctx.get(), GRPC_X509_SAN_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_PEM_CERT_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx.get(),
                                     GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_PEM_CERT_CHAIN_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx.get(),
                                     GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_SSL_SESSION_REUSED_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx.get(),
                                     GRPC_SSL_SESSION_REUSED_PROPERTY,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_SECURITY_LEVEL_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(
          ctx.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME,
          prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_DNS_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx.get(), GRPC_PEER_DNS_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_URI_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx.get(), GRPC_PEER_URI_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
      uri_count++;
      absl::string_view spiffe_id(prop->value.data, prop->value.length);
      if (IsSpiffeId(spiffe_id)) {
        spiffe_data = prop->value.data;
        spiffe_length = prop->value.length;
        has_spiffe_id = true;
      }
    } else if (strcmp(prop->name, TSI_X509_EMAIL_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx.get(), GRPC_PEER_EMAIL_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_IP_PEER_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx.get(), GRPC_PEER_IP_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    }
  }
  if (peer_identity_property_name != nullptr) {
    GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                   ctx.get(), peer_identity_property_name) == 1);
  }
  // A valid SPIFFE certificate can only have exact one URI SAN field.
  if (has_spiffe_id) {
    if (uri_count == 1) {
      GPR_ASSERT(spiffe_length > 0);
      GPR_ASSERT(spiffe_data != nullptr);
      grpc_auth_context_add_property(ctx.get(),
                                     GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
                                     spiffe_data, spiffe_length);
    } else {
      gpr_log(GPR_INFO, "Invalid SPIFFE ID: multiple URI SANs.");
    }
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
      } else if (strcmp(prop->name, GRPC_X509_SUBJECT_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_SUBJECT_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_X509_CN_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(
            &peer, prop, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_X509_PEM_CERT_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_PEM_CERT_PROPERTY);
      } else if (strcmp(prop->name,
                        GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_SECURITY_LEVEL_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME) ==
                 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_PEM_CERT_CHAIN_PROPERTY);
      } else if (strcmp(prop->name, GRPC_PEER_DNS_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_DNS_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_PEER_URI_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_URI_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_URI_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_PEER_EMAIL_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_EMAIL_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_PEER_IP_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_IP_PEER_PROPERTY);
      }
    }
  }
  return peer;
}

void grpc_shallow_peer_destruct(tsi_peer* peer) {
  if (peer->properties != nullptr) gpr_free(peer->properties);
}

grpc_security_status grpc_ssl_tsi_client_handshaker_factory_init(
    tsi_ssl_pem_key_cert_pair* pem_key_cert_pair, const char* pem_root_certs,
    bool skip_server_certificate_verification, tsi_tls_version min_tls_version,
    tsi_tls_version max_tls_version, tsi_ssl_session_cache* ssl_session_cache,
    const char* crl_directory,
    tsi_ssl_client_handshaker_factory** handshaker_factory) {
  const char* root_certs;
  const tsi_ssl_root_certs_store* root_store;
  if (pem_root_certs == nullptr) {
    gpr_log(GPR_INFO,
            "No root certificates specified; use ones stored in system default "
            "locations instead");
    // Use default root certificates.
    root_certs = grpc_core::DefaultSslRootStore::GetPemRootCerts();
    if (root_certs == nullptr) {
      gpr_log(GPR_ERROR, "Could not get default pem root certs.");
      return GRPC_SECURITY_ERROR;
    }
    root_store = grpc_core::DefaultSslRootStore::GetRootStore();
  } else {
    root_certs = pem_root_certs;
    root_store = nullptr;
  }
  bool has_key_cert_pair = pem_key_cert_pair != nullptr &&
                           pem_key_cert_pair->private_key != nullptr &&
                           pem_key_cert_pair->cert_chain != nullptr;
  tsi_ssl_client_handshaker_options options;
  GPR_DEBUG_ASSERT(root_certs != nullptr);
  options.pem_root_certs = root_certs;
  options.root_store = root_store;
  options.alpn_protocols =
      grpc_fill_alpn_protocol_strings(&options.num_alpn_protocols);
  if (has_key_cert_pair) {
    options.pem_key_cert_pair = pem_key_cert_pair;
  }
  options.cipher_suites = grpc_get_ssl_cipher_suites();
  options.session_cache = ssl_session_cache;
  options.skip_server_certificate_verification =
      skip_server_certificate_verification;
  options.min_tls_version = min_tls_version;
  options.max_tls_version = max_tls_version;
  options.crl_directory = crl_directory;
  const tsi_result result =
      tsi_create_ssl_client_handshaker_factory_with_options(&options,
                                                            handshaker_factory);
  gpr_free(options.alpn_protocols);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    return GRPC_SECURITY_ERROR;
  }
  return GRPC_SECURITY_OK;
}

grpc_security_status grpc_ssl_tsi_server_handshaker_factory_init(
    tsi_ssl_pem_key_cert_pair* pem_key_cert_pairs, size_t num_key_cert_pairs,
    const char* pem_root_certs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    tsi_tls_version min_tls_version, tsi_tls_version max_tls_version,
    const char* crl_directory,
    tsi_ssl_server_handshaker_factory** handshaker_factory) {
  size_t num_alpn_protocols = 0;
  const char** alpn_protocol_strings =
      grpc_fill_alpn_protocol_strings(&num_alpn_protocols);
  tsi_ssl_server_handshaker_options options;
  options.pem_key_cert_pairs = pem_key_cert_pairs;
  options.num_key_cert_pairs = num_key_cert_pairs;
  options.pem_client_root_certs = pem_root_certs;
  options.client_certificate_request =
      grpc_get_tsi_client_certificate_request_type(client_certificate_request);
  options.cipher_suites = grpc_get_ssl_cipher_suites();
  options.alpn_protocols = alpn_protocol_strings;
  options.num_alpn_protocols = static_cast<uint16_t>(num_alpn_protocols);
  options.min_tls_version = min_tls_version;
  options.max_tls_version = max_tls_version;
  options.crl_directory = crl_directory;
  const tsi_result result =
      tsi_create_ssl_server_handshaker_factory_with_options(&options,
                                                            handshaker_factory);
  gpr_free(alpn_protocol_strings);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    return GRPC_SECURITY_ERROR;
  }
  return GRPC_SECURITY_OK;
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
  return grpc_core::QsortCompare(p, q);
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

/* --- Default SSL root store implementation. --- */

namespace grpc_core {

tsi_ssl_root_certs_store* DefaultSslRootStore::default_root_store_;
grpc_slice DefaultSslRootStore::default_pem_root_certs_;

const tsi_ssl_root_certs_store* DefaultSslRootStore::GetRootStore() {
  InitRootStore();
  return default_root_store_;
}

const char* DefaultSslRootStore::GetPemRootCerts() {
  InitRootStore();
  return GRPC_SLICE_IS_EMPTY(default_pem_root_certs_)
             ? nullptr
             : reinterpret_cast<const char*>
                   GRPC_SLICE_START_PTR(default_pem_root_certs_);
}

grpc_slice DefaultSslRootStore::ComputePemRootCerts() {
  grpc_slice result = grpc_empty_slice();
  const bool not_use_system_roots =
      GPR_GLOBAL_CONFIG_GET(grpc_not_use_system_ssl_roots);
  // First try to load the roots from the configuration.
  UniquePtr<char> default_root_certs_path =
      GPR_GLOBAL_CONFIG_GET(grpc_default_ssl_roots_file_path);
  if (strlen(default_root_certs_path.get()) > 0) {
    GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(default_root_certs_path.get(), 1, &result));
  }
  // Try overridden roots if needed.
  grpc_ssl_roots_override_result ovrd_res = GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  if (GRPC_SLICE_IS_EMPTY(result) && ssl_roots_override_cb != nullptr) {
    char* pem_root_certs = nullptr;
    ovrd_res = ssl_roots_override_cb(&pem_root_certs);
    if (ovrd_res == GRPC_SSL_ROOTS_OVERRIDE_OK) {
      GPR_ASSERT(pem_root_certs != nullptr);
      result = grpc_slice_from_copied_buffer(
          pem_root_certs,
          strlen(pem_root_certs) + 1);  // nullptr terminator.
    }
    gpr_free(pem_root_certs);
  }
  // Try loading roots from OS trust store if flag is enabled.
  if (GRPC_SLICE_IS_EMPTY(result) && !not_use_system_roots) {
    result = LoadSystemRootCerts();
  }
  // Fallback to roots manually shipped with gRPC.
  if (GRPC_SLICE_IS_EMPTY(result) &&
      ovrd_res != GRPC_SSL_ROOTS_OVERRIDE_FAIL_PERMANENTLY) {
    GRPC_LOG_IF_ERROR("load_file",
                      grpc_load_file(installed_roots_path, 1, &result));
  }
  return result;
}

void DefaultSslRootStore::InitRootStore() {
  static gpr_once once = GPR_ONCE_INIT;
  gpr_once_init(&once, DefaultSslRootStore::InitRootStoreOnce);
}

void DefaultSslRootStore::InitRootStoreOnce() {
  default_pem_root_certs_ = ComputePemRootCerts();
  if (!GRPC_SLICE_IS_EMPTY(default_pem_root_certs_)) {
    default_root_store_ =
        tsi_ssl_root_certs_store_create(reinterpret_cast<const char*>(
            GRPC_SLICE_START_PTR(default_pem_root_certs_)));
  }
}

}  // namespace grpc_core
