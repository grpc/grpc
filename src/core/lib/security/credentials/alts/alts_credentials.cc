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
#include "src/core/lib/security/security_connector/alts_security_connector.h"

#define GRPC_CREDENTIALS_TYPE_ALTS "Alts"
#define GRPC_ALTS_HANDSHAKER_SERVICE_URL "metadata.google.internal:8080"

static void alts_credentials_destruct(grpc_channel_credentials* creds) {
  grpc_alts_credentials* alts_creds =
      reinterpret_cast<grpc_alts_credentials*>(creds);
  grpc_alts_credentials_options_destroy(alts_creds->options);
  gpr_free(alts_creds->handshaker_service_url);
}

static void alts_server_credentials_destruct(grpc_server_credentials* creds) {
  grpc_alts_server_credentials* alts_creds =
      reinterpret_cast<grpc_alts_server_credentials*>(creds);
  grpc_alts_credentials_options_destroy(alts_creds->options);
  gpr_free(alts_creds->handshaker_service_url);
}

static grpc_security_status alts_create_security_connector(
    grpc_channel_credentials* creds,
    grpc_call_credentials* request_metadata_creds, const char* target_name,
    const grpc_channel_args* args, grpc_channel_security_connector** sc,
    grpc_channel_args** new_args) {
  return grpc_alts_channel_security_connector_create(
      creds, request_metadata_creds, target_name, sc);
}

static grpc_security_status alts_server_create_security_connector(
    grpc_server_credentials* creds, grpc_server_security_connector** sc) {
  return grpc_alts_server_security_connector_create(creds, sc);
}

static const grpc_channel_credentials_vtable alts_credentials_vtable = {
    alts_credentials_destruct, alts_create_security_connector,
    /*duplicate_without_call_credentials=*/nullptr};

static const grpc_server_credentials_vtable alts_server_credentials_vtable = {
    alts_server_credentials_destruct, alts_server_create_security_connector};

grpc_channel_credentials* grpc_alts_credentials_create_customized(
    const grpc_alts_credentials_options* options,
    const char* handshaker_service_url, bool enable_untrusted_alts) {
  if (!enable_untrusted_alts && !grpc_alts_is_running_on_gcp()) {
    return nullptr;
  }
  auto creds = static_cast<grpc_alts_credentials*>(
      gpr_zalloc(sizeof(grpc_alts_credentials)));
  creds->options = grpc_alts_credentials_options_copy(options);
  creds->handshaker_service_url =
      handshaker_service_url == nullptr
          ? gpr_strdup(GRPC_ALTS_HANDSHAKER_SERVICE_URL)
          : gpr_strdup(handshaker_service_url);
  creds->base.type = GRPC_CREDENTIALS_TYPE_ALTS;
  creds->base.vtable = &alts_credentials_vtable;
  gpr_ref_init(&creds->base.refcount, 1);
  return &creds->base;
}

grpc_server_credentials* grpc_alts_server_credentials_create_customized(
    const grpc_alts_credentials_options* options,
    const char* handshaker_service_url, bool enable_untrusted_alts) {
  if (!enable_untrusted_alts && !grpc_alts_is_running_on_gcp()) {
    return nullptr;
  }
  auto creds = static_cast<grpc_alts_server_credentials*>(
      gpr_zalloc(sizeof(grpc_alts_server_credentials)));
  creds->options = grpc_alts_credentials_options_copy(options);
  creds->handshaker_service_url =
      handshaker_service_url == nullptr
          ? gpr_strdup(GRPC_ALTS_HANDSHAKER_SERVICE_URL)
          : gpr_strdup(handshaker_service_url);
  creds->base.type = GRPC_CREDENTIALS_TYPE_ALTS;
  creds->base.vtable = &alts_server_credentials_vtable;
  gpr_ref_init(&creds->base.refcount, 1);
  return &creds->base;
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
