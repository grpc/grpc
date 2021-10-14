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

#include "src/core/lib/security/credentials/tls/tls_credentials.h"

#include <cstring>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"
#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"

#define GRPC_CREDENTIALS_TYPE_TLS "Tls"

namespace {

bool CredentialOptionSanityCheck(grpc_tls_credentials_options* options,
                                 bool is_client) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR, "TLS credentials options is nullptr.");
    return false;
  }
  // In the following conditions, there won't be any issues, but it might
  // indicate callers are doing something wrong with the API.
  if (is_client && options->cert_request_type() !=
                       GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE) {
    gpr_log(GPR_ERROR,
            "Client's credentials options should not set cert_request_type.");
  }
  if (!is_client && !options->verify_server_cert()) {
    gpr_log(GPR_ERROR,
            "Server's credentials options should not set verify_server_cert.");
  }
  // In the following conditions, there could be severe security issues.
  if (is_client && options->certificate_verifier() == nullptr) {
    // If no verifier is specified on the client side, use the hostname verifier
    // as default. Users who want to bypass all the verifier check should
    // implement an external verifier instead.
    gpr_log(GPR_INFO,
            "No verifier specified on the client side. Using default hostname "
            "verifier");
    options->set_certificate_verifier(
        grpc_core::MakeRefCounted<grpc_core::HostNameCertificateVerifier>());
  }
  return true;
}

}  // namespace

TlsCredentials::TlsCredentials(
    grpc_core::RefCountedPtr<grpc_tls_credentials_options> options)
    : grpc_channel_credentials(GRPC_CREDENTIALS_TYPE_TLS),
      options_(std::move(options)) {}

TlsCredentials::~TlsCredentials() {}

grpc_core::RefCountedPtr<grpc_channel_security_connector>
TlsCredentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target_name, const grpc_channel_args* args,
    grpc_channel_args** new_args) {
  const char* overridden_target_name = nullptr;
  tsi_ssl_session_cache* ssl_session_cache = nullptr;
  for (size_t i = 0; args != nullptr && i < args->num_args; i++) {
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
      grpc_core::TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
          this->Ref(), options_, std::move(call_creds), target_name,
          overridden_target_name, ssl_session_cache);
  if (sc == nullptr) {
    return nullptr;
  }
  if (args != nullptr) {
    grpc_arg new_arg = grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_HTTP2_SCHEME), const_cast<char*>("https"));
    *new_args = grpc_channel_args_copy_and_add(args, &new_arg, 1);
  }
  return sc;
}

TlsServerCredentials::TlsServerCredentials(
    grpc_core::RefCountedPtr<grpc_tls_credentials_options> options)
    : grpc_server_credentials(GRPC_CREDENTIALS_TYPE_TLS),
      options_(std::move(options)) {}

TlsServerCredentials::~TlsServerCredentials() {}

grpc_core::RefCountedPtr<grpc_server_security_connector>
TlsServerCredentials::create_security_connector(
    const grpc_channel_args* /* args */) {
  return grpc_core::TlsServerSecurityConnector::
      CreateTlsServerSecurityConnector(this->Ref(), options_);
}

/** -- Wrapper APIs declared in grpc_security.h -- **/

grpc_channel_credentials* grpc_tls_credentials_create(
    grpc_tls_credentials_options* options) {
  if (!CredentialOptionSanityCheck(options, true /* is_client */)) {
    return nullptr;
  }
  return new TlsCredentials(
      grpc_core::RefCountedPtr<grpc_tls_credentials_options>(options));
}

grpc_server_credentials* grpc_tls_server_credentials_create(
    grpc_tls_credentials_options* options) {
  if (!CredentialOptionSanityCheck(options, false /* is_client */)) {
    return nullptr;
  }
  return new TlsServerCredentials(
      grpc_core::RefCountedPtr<grpc_tls_credentials_options>(options));
}
