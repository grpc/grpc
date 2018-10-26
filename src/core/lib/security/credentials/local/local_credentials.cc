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

#include "src/core/lib/security/credentials/local/local_credentials.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/security_connector/local/local_security_connector.h"

#define GRPC_CREDENTIALS_TYPE_LOCAL "Local"

static void local_credentials_destruct(grpc_channel_credentials* creds) {}

static void local_server_credentials_destruct(grpc_server_credentials* creds) {}

static grpc_security_status local_create_security_connector(
    grpc_channel_credentials* creds,
    grpc_call_credentials* request_metadata_creds, const char* target_name,
    const grpc_channel_args* args, grpc_channel_security_connector** sc,
    grpc_channel_args** new_args) {
  return grpc_local_channel_security_connector_create(
      creds, request_metadata_creds, args, target_name, sc);
}

static grpc_security_status local_server_create_security_connector(
    grpc_server_credentials* creds, grpc_server_security_connector** sc) {
  return grpc_local_server_security_connector_create(creds, sc);
}

static const grpc_channel_credentials_vtable local_credentials_vtable = {
    local_credentials_destruct, local_create_security_connector,
    /*duplicate_without_call_credentials=*/nullptr};

static const grpc_server_credentials_vtable local_server_credentials_vtable = {
    local_server_credentials_destruct, local_server_create_security_connector};

grpc_channel_credentials* grpc_local_credentials_create(
    grpc_local_connect_type connect_type) {
  auto creds = static_cast<grpc_local_credentials*>(
      gpr_zalloc(sizeof(grpc_local_credentials)));
  creds->connect_type = connect_type;
  creds->base.type = GRPC_CREDENTIALS_TYPE_LOCAL;
  creds->base.vtable = &local_credentials_vtable;
  gpr_ref_init(&creds->base.refcount, 1);
  return &creds->base;
}

grpc_server_credentials* grpc_local_server_credentials_create(
    grpc_local_connect_type connect_type) {
  auto creds = static_cast<grpc_local_server_credentials*>(
      gpr_zalloc(sizeof(grpc_local_server_credentials)));
  creds->connect_type = connect_type;
  creds->base.type = GRPC_CREDENTIALS_TYPE_LOCAL;
  creds->base.vtable = &local_server_credentials_vtable;
  gpr_ref_init(&creds->base.refcount, 1);
  return &creds->base;
}
