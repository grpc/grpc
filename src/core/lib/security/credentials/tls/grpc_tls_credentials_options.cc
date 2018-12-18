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

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

/** -- TLS credentials options API implementation. -- **/
grpc_tls_credentials_options* grpc_tls_credentials_options_create() {
  return static_cast<grpc_tls_credentials_options*>(
      gpr_zalloc(sizeof(grpc_tls_credentials_options)));
}

void grpc_tls_credentials_options_set_cert_request_type(
    grpc_tls_credentials_options* options,
    grpc_ssl_client_certificate_request_type type) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_cert_request_type()");
    return;
  }
  options->cert_request_type = type;
}

void grpc_tls_credentials_options_set_key_materials_config(
    grpc_tls_credentials_options* options,
    grpc_tls_key_materials_config* config) {
  if (options == nullptr || config == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_key_materials_config()");
    return;
  }
  options->key_materials_config = config;
}

void grpc_tls_credentials_options_set_credential_reload_config(
    grpc_tls_credentials_options* options,
    grpc_tls_credential_reload_config* config) {
  if (options == nullptr || config == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_credential_reload_config()");
    return;
  }
  options->credential_reload_config = config;
}

void grpc_tls_credentials_options_set_server_authorization_check_config(
    grpc_tls_credentials_options* options,
    grpc_tls_server_authorization_check_config* config) {
  if (options == nullptr || config == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid nullptr arguments to "
        "grpc_tls_credentials_options_set_server_authorization_check_config()");
    return;
  }
  options->server_authorization_check_config = config;
}

void grpc_tls_credentials_options_destroy(
    grpc_tls_credentials_options* options) {
  if (options == nullptr) {
    return;
  }
  grpc_tls_key_materials_config_destroy(options->key_materials_config);
  grpc_tls_credential_reload_config_unref(options->credential_reload_config);
  grpc_tls_server_authorization_check_config_unref(
      options->server_authorization_check_config);
  gpr_free(options);
}

/** -- TLS key materials config API implementation. -- **/
GRPCAPI grpc_tls_key_materials_config* grpc_tls_key_materials_config_create() {
  return static_cast<grpc_tls_key_materials_config*>(
      gpr_zalloc(sizeof(grpc_tls_key_materials_config)));
}

void grpc_tls_key_materials_config_set_key_materials(
    grpc_tls_key_materials_config* config,
    grpc_ssl_pem_key_cert_pair* key_cert_pairs, const char* root_certs,
    size_t num) {
  if (config == nullptr || key_cert_pairs == nullptr || num == 0) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_tls_key_materials_config_set_key_materials()");
    return;
  }
  config->pem_key_cert_pairs = static_cast<grpc_ssl_pem_key_cert_pair*>(
      gpr_zalloc(sizeof(grpc_ssl_pem_key_cert_pair) * num));
  for (size_t i = 0; i < num; i++) {
    config->pem_key_cert_pairs[i].private_key =
        gpr_strdup(key_cert_pairs[i].private_key);
    config->pem_key_cert_pairs[i].cert_chain =
        gpr_strdup(key_cert_pairs[i].cert_chain);
  }
  config->pem_root_certs = gpr_strdup(root_certs);
  config->num_key_cert_pairs = num;
}

void grpc_tls_key_materials_config_destroy(
    grpc_tls_key_materials_config* config) {
  if (config == nullptr) {
    return;
  }
  for (size_t i = 0; i < config->num_key_cert_pairs; i++) {
    gpr_free((void*)config->pem_key_cert_pairs[i].private_key);
    gpr_free((void*)config->pem_key_cert_pairs[i].cert_chain);
  }
  gpr_free(config->pem_key_cert_pairs);
  gpr_free((void*)config->pem_root_certs);
  gpr_free(config);
}

static grpc_tls_key_materials_config* key_materials_config_copy(
    grpc_tls_key_materials_config* config) {
  if (config == nullptr || config->pem_key_cert_pairs == nullptr ||
      config->num_key_cert_pairs == 0) {
    return nullptr;
  }
  grpc_tls_key_materials_config* new_config =
      static_cast<grpc_tls_key_materials_config*>(
          gpr_zalloc(sizeof(*new_config)));
  new_config->pem_root_certs = gpr_strdup(config->pem_root_certs);
  grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs =
      static_cast<grpc_ssl_pem_key_cert_pair*>(gpr_zalloc(
          sizeof(grpc_ssl_pem_key_cert_pair) * config->num_key_cert_pairs));
  for (size_t i = 0; i < config->num_key_cert_pairs; i++) {
    pem_key_cert_pairs[i].private_key =
        gpr_strdup(config->pem_key_cert_pairs[i].private_key);
    pem_key_cert_pairs[i].cert_chain =
        gpr_strdup(config->pem_key_cert_pairs[i].cert_chain);
  }
  new_config->pem_key_cert_pairs = pem_key_cert_pairs;
  new_config->num_key_cert_pairs = config->num_key_cert_pairs;
  return new_config;
}

/** -- TLS credential reload config API implementation. -- **/
grpc_tls_credential_reload_config* grpc_tls_credential_reload_config_create(
    const void* config_user_data,
    int (*schedule)(void* config_user_data,
                    grpc_tls_credential_reload_arg* arg),
    void (*cancel)(void* config_user_data, grpc_tls_credential_reload_arg* arg),
    void (*destruct)(void* config_user_data)) {
  if (schedule == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Schedule API is nullptr in creating TLS credential reload config.");
    return nullptr;
  }
  grpc_tls_credential_reload_config* config =
      static_cast<grpc_tls_credential_reload_config*>(
          gpr_zalloc(sizeof(*config)));
  config->config_user_data = config_user_data;
  config->schedule = schedule;
  config->cancel = cancel;
  config->destruct = destruct;
  gpr_ref_init(&config->refcount, 1);
  return config;
}

grpc_tls_credential_reload_config* grpc_tls_credential_reload_config_ref(
    grpc_tls_credential_reload_config* config) {
  if (config == nullptr) {
    return nullptr;
  }
  gpr_ref(&config->refcount);
  return config;
}

void grpc_tls_credential_reload_config_unref(
    grpc_tls_credential_reload_config* config) {
  if (config == nullptr || !gpr_unref(&config->refcount)) {
    return;
  }
  if (config->destruct != nullptr) {
    config->destruct((void*)config->config_user_data);
  }
  gpr_free(config);
}

/** -- TLS server authorization check config API implementation. -- **/
grpc_tls_server_authorization_check_config*
grpc_tls_server_authorization_check_config_create(
    const void* config_user_data,
    int (*schedule)(void* config_user_data,
                    grpc_tls_server_authorization_check_arg* arg),
    void (*cancel)(void* config_user_data,
                   grpc_tls_server_authorization_check_arg* arg),
    void (*destruct)(void* config_user_data)) {
  if (schedule == nullptr) {
    gpr_log(GPR_ERROR,
            "Schedule API is nullptr in creating TLS server authorization "
            "check config.");
    return nullptr;
  }
  grpc_tls_server_authorization_check_config* config =
      static_cast<grpc_tls_server_authorization_check_config*>(
          gpr_zalloc(sizeof(*config)));
  config->config_user_data = config_user_data;
  config->schedule = schedule;
  config->destruct = destruct;
  gpr_ref_init(&config->refcount, 1);
  return config;
}

grpc_tls_server_authorization_check_config*
grpc_tls_server_authorization_check_config_ref(
    grpc_tls_server_authorization_check_config* config) {
  if (config == nullptr) {
    return nullptr;
  }
  gpr_ref(&config->refcount);
  return config;
}

void grpc_tls_server_authorization_check_config_unref(
    grpc_tls_server_authorization_check_config* config) {
  if (config == nullptr || !gpr_unref(&config->refcount)) {
    return;
  }
  if (config->destruct != nullptr) {
    config->destruct((void*)config->config_user_data);
  }
  gpr_free(config);
}

grpc_tls_credentials_options* grpc_tls_credentials_options_copy(
    const grpc_tls_credentials_options* options) {
  if (options == nullptr) {
    return nullptr;
  }
  grpc_tls_credentials_options* new_options =
      grpc_tls_credentials_options_create();
  new_options->cert_request_type = options->cert_request_type;
  new_options->key_materials_config =
      key_materials_config_copy(options->key_materials_config);
  new_options->credential_reload_config =
      grpc_tls_credential_reload_config_ref(options->credential_reload_config);
  new_options->server_authorization_check_config =
      grpc_tls_server_authorization_check_config_ref(
          options->server_authorization_check_config);
  return new_options;
}
