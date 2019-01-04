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

#include "src/core/lib/security/credentials/tls/spiffe_credentials.h"

#include <cstring>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/security_connector/tls/spiffe_security_connector.h"

#define GRPC_CREDENTIALS_TYPE_SPIFFE "Spiffe"

grpc_tls_spiffe_credentials::grpc_tls_spiffe_credentials(
    const grpc_tls_credentials_options* options)
    : grpc_channel_credentials(GRPC_CREDENTIALS_TYPE_SPIFFE),
      options_(const_cast<grpc_tls_credentials_options*>(options)->Ref()) {}

grpc_tls_spiffe_credentials::~grpc_tls_spiffe_credentials() {
  options_.get()->Unref();
}

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_tls_spiffe_credentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target_name, const grpc_channel_args* args,
    grpc_channel_args** new_args) {
  const char* overridden_target_name = nullptr;
  tsi_ssl_session_cache* ssl_session_cache = nullptr;
  for (size_t i = 0; args && i < args->num_args; i++) {
    grpc_arg* arg = &args->args[i];
    if (strcmp(arg->key, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG) == 0 &&
        arg->type == GRPC_ARG_STRING) {
      overridden_target_name = arg->value.string;
    }
    if (strcmp(arg->key, GRPC_SSL_SESSION_CACHE_ARG) == 0 &&
        arg->type == GRPC_ARG_POINTER) {
      ssl_session_cache =
          static_cast<tsi_ssl_session_cache*>(arg->value.pointer.p);
    }
  }
  grpc_core::RefCountedPtr<grpc_channel_security_connector> sc =
      grpc_tls_spiffe_channel_security_connector_create(
          this->Ref(), std::move(call_creds), target_name,
          overridden_target_name, ssl_session_cache);
  if (sc == nullptr) {
    return sc;
  }
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_HTTP2_SCHEME, (char*)"https");
  *new_args = grpc_channel_args_copy_and_add(args, &new_arg, 1);
  return sc;
}

grpc_tls_spiffe_server_credentials::grpc_tls_spiffe_server_credentials(
    const grpc_tls_credentials_options* options)
    : grpc_server_credentials(GRPC_CREDENTIALS_TYPE_SPIFFE),
      options_(std::move(
          const_cast<grpc_tls_credentials_options*>(options)->Ref())) {
  options_->set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
}

grpc_tls_spiffe_server_credentials::~grpc_tls_spiffe_server_credentials() {
  options_.get()->Unref();
}

grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_tls_spiffe_server_credentials::create_security_connector() {
  return grpc_tls_spiffe_server_security_connector_create(this->Ref());
}

static bool credentials_options_sanity_check(
    const grpc_tls_credentials_options* options) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR, "SPIFFE TLS credentials options is nullptr.");
    return false;
  }
  if (options->key_materials_config() == nullptr &&
      options->credential_reload_config() == nullptr) {
    gpr_log(
        GPR_ERROR,
        "SPIFFE TLS credentials options must specify either key materials or "
        "credential reload config.");
    return false;
  }
  return true;
}

grpc_channel_credentials* grpc_tls_spiffe_credentials_create(
    const grpc_tls_credentials_options* options) {
  if (!credentials_options_sanity_check(options)) {
    gpr_log(GPR_ERROR,
            "Invalid credentials options in creating TLS spiffe channel "
            "credentials.");
    return nullptr;
  }
  return grpc_core::New<grpc_tls_spiffe_credentials>(options);
}

grpc_server_credentials* grpc_tls_spiffe_server_credentials_create(
    const grpc_tls_credentials_options* options) {
  if (!credentials_options_sanity_check(options)) {
    gpr_log(GPR_ERROR,
            "Invalid credentials options in creating TLS spiffe server "
            "credentials.");
    return nullptr;
  }
  return grpc_core::New<grpc_tls_spiffe_server_credentials>(options);
}
