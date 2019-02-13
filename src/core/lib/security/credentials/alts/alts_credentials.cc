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

#include "src/core/lib/security/credentials/alts/alts_credentials.h"

#include <cstring>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"
#include "src/core/lib/security/security_connector/alts/alts_security_connector.h"

#define GRPC_CREDENTIALS_TYPE_ALTS "Alts"
#define GRPC_ALTS_HANDSHAKER_SERVICE_URL "metadata.google.internal.:8080"

grpc_alts_credentials::grpc_alts_credentials(
    const grpc_alts_credentials_options* options,
    const char* handshaker_service_url)
    : grpc_channel_credentials(GRPC_CREDENTIALS_TYPE_ALTS),
      options_(grpc_alts_credentials_options_copy(options)),
      handshaker_service_url_(handshaker_service_url == nullptr
                                  ? gpr_strdup(GRPC_ALTS_HANDSHAKER_SERVICE_URL)
                                  : gpr_strdup(handshaker_service_url)) {}

grpc_alts_credentials::~grpc_alts_credentials() {
  grpc_alts_credentials_options_destroy(options_);
  gpr_free(handshaker_service_url_);
}

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_alts_credentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target_name, const grpc_channel_args* args,
    grpc_channel_args** new_args) {
  return grpc_alts_channel_security_connector_create(
      this->Ref(), std::move(call_creds), target_name);
}

grpc_alts_server_credentials::grpc_alts_server_credentials(
    const grpc_alts_credentials_options* options,
    const char* handshaker_service_url)
    : grpc_server_credentials(GRPC_CREDENTIALS_TYPE_ALTS),
      options_(grpc_alts_credentials_options_copy(options)),
      handshaker_service_url_(handshaker_service_url == nullptr
                                  ? gpr_strdup(GRPC_ALTS_HANDSHAKER_SERVICE_URL)
                                  : gpr_strdup(handshaker_service_url)) {}

grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_alts_server_credentials::create_security_connector() {
  return grpc_alts_server_security_connector_create(this->Ref());
}

grpc_alts_server_credentials::~grpc_alts_server_credentials() {
  grpc_alts_credentials_options_destroy(options_);
  gpr_free(handshaker_service_url_);
}

grpc_channel_credentials* grpc_alts_credentials_create_customized(
    const grpc_alts_credentials_options* options,
    const char* handshaker_service_url, bool enable_untrusted_alts) {
  if (!enable_untrusted_alts && !grpc_alts_is_running_on_gcp()) {
    return nullptr;
  }
  return grpc_core::New<grpc_alts_credentials>(options, handshaker_service_url);
}

grpc_server_credentials* grpc_alts_server_credentials_create_customized(
    const grpc_alts_credentials_options* options,
    const char* handshaker_service_url, bool enable_untrusted_alts) {
  if (!enable_untrusted_alts && !grpc_alts_is_running_on_gcp()) {
    return nullptr;
  }
  return grpc_core::New<grpc_alts_server_credentials>(options,
                                                      handshaker_service_url);
}

grpc_channel_credentials* grpc_alts_credentials_create(
    const grpc_alts_credentials_options* options) {
  return grpc_alts_credentials_create_customized(
      options, GRPC_ALTS_HANDSHAKER_SERVICE_URL, false);
}

grpc_server_credentials* grpc_alts_server_credentials_create(
    const grpc_alts_credentials_options* options) {
  return grpc_alts_server_credentials_create_customized(
      options, GRPC_ALTS_HANDSHAKER_SERVICE_URL, false);
}
