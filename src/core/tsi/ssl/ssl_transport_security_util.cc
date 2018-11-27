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

#include <grpc/grpc_security.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/tsi/ssl/ssl_transport_security_util.h"

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/tsi/ssl/load_system_roots.h"

/* -- Constants. -- */

#ifndef INSTALL_PREFIX
static const char* installed_roots_path = "/usr/share/grpc/roots.pem";
#else
static const char* installed_roots_path =
    INSTALL_PREFIX "/share/grpc/roots.pem";
#endif

/** Environment variable used as a flag to enable/disable loading system root
    certificates from the OS trust store. */
#ifndef GRPC_NOT_USE_SYSTEM_SSL_ROOTS_ENV_VAR
#define GRPC_NOT_USE_SYSTEM_SSL_ROOTS_ENV_VAR "GRPC_NOT_USE_SYSTEM_SSL_ROOTS"
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

/* Defines the cipher suites that we accept by default. All these cipher suites
   are compliant with HTTP2. */
#define GRPC_SSL_CIPHER_SUITES     \
  "ECDHE-ECDSA-AES128-GCM-SHA256:" \
  "ECDHE-ECDSA-AES256-GCM-SHA384:" \
  "ECDHE-RSA-AES128-GCM-SHA256:"   \
  "ECDHE-RSA-AES256-GCM-SHA384"

static gpr_once cipher_suites_once = GPR_ONCE_INIT;
static const char* cipher_suites = nullptr;

static void init_cipher_suites(void) {
  char* overridden = gpr_getenv("GRPC_SSL_CIPHER_SUITES");
  cipher_suites = overridden != nullptr ? overridden : GRPC_SSL_CIPHER_SUITES;
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

tsi_ssl_pem_key_cert_pair* grpc_convert_grpc_to_tsi_cert_pairs(
    const grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs) {
  tsi_ssl_pem_key_cert_pair* tsi_pairs = nullptr;
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(pem_key_cert_pairs != nullptr);
    tsi_pairs = static_cast<tsi_ssl_pem_key_cert_pair*>(
        gpr_zalloc(num_key_cert_pairs * sizeof(tsi_ssl_pem_key_cert_pair)));
  }
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(pem_key_cert_pairs[i].private_key != nullptr);
    GPR_ASSERT(pem_key_cert_pairs[i].cert_chain != nullptr);
    tsi_pairs[i].cert_chain = gpr_strdup(pem_key_cert_pairs[i].cert_chain);
    tsi_pairs[i].private_key = gpr_strdup(pem_key_cert_pairs[i].private_key);
  }
  return tsi_pairs;
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
  char* not_use_system_roots_env_value =
      gpr_getenv(GRPC_NOT_USE_SYSTEM_SSL_ROOTS_ENV_VAR);
  const bool not_use_system_roots = gpr_is_true(not_use_system_roots_env_value);
  gpr_free(not_use_system_roots_env_value);
  // First try to load the roots from the environment.
  char* default_root_certs_path =
      gpr_getenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR);
  if (default_root_certs_path != nullptr) {
    GRPC_LOG_IF_ERROR("load_file",
                      grpc_load_file(default_root_certs_path, 1, &result));
    gpr_free(default_root_certs_path);
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
