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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

struct grpc_tls_credentials_options {
  grpc_ssl_client_certificate_request_type cert_request_type;
  grpc_tls_key_materials_config* key_materials_config;
  grpc_tls_credential_reload_config* credential_reload_config;
  grpc_tls_server_authorization_check_config* server_authorization_check_config;
};

/** TLS key materials config. **/
struct grpc_tls_key_materials_config {
  grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs;
  size_t num_key_cert_pairs;
  const char* pem_root_certs;
};

/** TLS credential reload config. **/
struct grpc_tls_credential_reload_config {
  /** config-specific, read-only user data that works for all channels created
     with a credential using the config. */
  const void* config_user_data;

  /** callback function for invoking credential reload API. The implementation
     of this method has to be non-blocking, but can be performed synchronously
     or asynchronously.
      If processing occurs synchronously, it populates \a arg->key_materials, \a
      arg->status, and \a arg->error_details and returns zero.
      If processing occurs asynchronously, it returns a non-zero value.
     Application then invokes \a arg->cb when processing is completed. Note that
     \a arg->cb cannot be invoked before \a schedule returns.
  */
  int (*schedule)(void* config_user_data, grpc_tls_credential_reload_arg* arg);

  /** callback function for cancelling a credential reload request scheduled via
     an asynchronous \a schedule. \a arg is used to pinpoint an exact reloading
      request to be cancelled, and the operation may not have any effect if the
     request has already been processed. */
  void (*cancel)(void* config_user_data, grpc_tls_credential_reload_arg* arg);

  /** callback function for cleaning up any data associated with credential
   * reload config. */
  void (*destruct)(void* config_user_data);

  /* refcount for config. */
  gpr_refcount refcount;
};

/** TLS server authorization check config. **/
struct grpc_tls_server_authorization_check_config {
  /** config-specific, read-only user data that works for all channels created
     with a Credential using the config. */
  const void* config_user_data;

  /** callback function for invoking server authorization check. The
     implementation of this method has to be non-blocking, but can be performed
     synchronously or asynchronously.
      If processing occurs synchronously, it populates \a arg->result, \a
     arg->status, and \a arg->error_details, and returns zero.
      If processing occurs asynchronously, it returns a non-zero value.
     Application then invokes \a arg->cb when processing is completed. Note that
     \a arg->cb cannot be invoked before \a schedule() returns.
  */
  int (*schedule)(void* config_user_data,
                  grpc_tls_server_authorization_check_arg* arg);

  /** callback function for canceling a server authorization check request. */
  void (*cancel)(void* config_user_data,
                 grpc_tls_server_authorization_check_arg* arg);

  /** callback function for cleaning up any data associated with server
     authorization check config. */
  void (*destruct)(void* config_user_data);

  /* refcount for config. */
  gpr_refcount refcount;
};

/**
 * This method performs a deep copy on grpc_tls_credentials_options instance.
 *
 * - options: a grpc_tls_credentials_options instance that needs to be copied.
 *
 * It returns a new grpc_tls_credentials_options instance on success and NULL
 * on failure.
 */
grpc_tls_credentials_options* grpc_tls_credentials_options_copy(
    const grpc_tls_credentials_options* options);

/** Destroy a grpc_tls_key_materials_config instance. */
void grpc_tls_key_materials_config_destroy(
    grpc_tls_key_materials_config* config);

/* API's for ref/unref credential reload and server authorization check configs.
 */
grpc_tls_credential_reload_config* grpc_tls_credential_reload_config_ref(
    grpc_tls_credential_reload_config* config);

void grpc_tls_credential_reload_config_unref(
    grpc_tls_credential_reload_config* config);

grpc_tls_server_authorization_check_config*
grpc_tls_server_authorization_check_config_ref(
    grpc_tls_server_authorization_check_config* config);

void grpc_tls_server_authorization_check_config_unref(
    grpc_tls_server_authorization_check_config* config);

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H \
        */
