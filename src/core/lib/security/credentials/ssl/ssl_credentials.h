/*
 *
 * Copyright 2016 gRPC authors.
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
#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_SSL_SSL_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_SSL_SSL_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/credentials.h"

#include "src/core/lib/security/security_connector/ssl/ssl_security_connector.h"

typedef struct {
  grpc_channel_credentials base;
  grpc_ssl_config config;
} grpc_ssl_credentials;

struct grpc_ssl_server_certificate_config {
  grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs;
  size_t num_key_cert_pairs;
  char* pem_root_certs;
};

typedef struct {
  grpc_ssl_server_certificate_config_callback cb;
  void* user_data;
} grpc_ssl_server_certificate_config_fetcher;

typedef struct {
  grpc_server_credentials base;
  grpc_ssl_server_config config;
  grpc_ssl_server_certificate_config_fetcher certificate_config_fetcher;
} grpc_ssl_server_credentials;

tsi_ssl_pem_key_cert_pair* grpc_convert_grpc_to_tsi_cert_pairs(
    const grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs);

void grpc_tsi_ssl_pem_key_cert_pairs_destroy(tsi_ssl_pem_key_cert_pair* kp,
                                             size_t num_key_cert_pairs);

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_SSL_SSL_CREDENTIALS_H */
