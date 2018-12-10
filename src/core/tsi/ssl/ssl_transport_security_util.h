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

#ifndef GRPC_CORE_TSI_SSL_SSL_TRANSPORT_SECURITY_UTIL_H
#define GRPC_CORE_TSI_SSL_SSL_TRANSPORT_SECURITY_UTIL_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

#include "src/core/tsi/transport_security_interface.h"

#include "src/core/tsi/ssl/ssl_transport_security.h"

/* Return HTTP2-compliant cipher suites that gRPC accepts by default. */
const char* tsi_get_ssl_cipher_suites(void);

/* Map from grpc_ssl_client_certificate_request_type to
 * tsi_client_certificate_request_type. */
tsi_client_certificate_request_type tsi_get_tsi_client_certificate_request_type(
    grpc_ssl_client_certificate_request_type grpc_request_type);

/* Return an array of strings containing alpn protocols. */
const char** tsi_fill_alpn_protocol_strings(size_t* num_alpn_protocols);

/* Convert a grpc_ssl_pem_key_cert_pair instance to tsi_ssl_pem_key_cert_pair
 * instance. */
tsi_ssl_pem_key_cert_pair* tsi_convert_grpc_to_tsi_cert_pairs(
    const grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs);

/* --- Default SSL Root Store. --- */
namespace tsi {

// The class implements default SSL root store.
class DefaultSslRootStore {
 public:
  // Gets the default SSL root store. Returns nullptr if not found.
  static const tsi_ssl_root_certs_store* GetRootStore();

  // Gets the default PEM root certificate.
  static const char* GetPemRootCerts();

 protected:
  // Returns default PEM root certificates in nullptr terminated grpc_slice.
  // This function is protected instead of private, so that it can be tested.
  static grpc_slice ComputePemRootCerts();

 private:
  // Construct me not!
  DefaultSslRootStore();

  // Initialization of default SSL root store.
  static void InitRootStore();

  // One-time initialization of default SSL root store.
  static void InitRootStoreOnce();

  // SSL root store in tsi_ssl_root_certs_store object.
  static tsi_ssl_root_certs_store* default_root_store_;

  // Default PEM root certificates.
  static grpc_slice default_pem_root_certs_;
};

}  // namespace tsi

#endif /* GRPC_CORE_TSI_SSL_SSL_TRANSPORT_SECURITY_UTIL_H \
        */
