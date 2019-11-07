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

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_local_credentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target_name, const grpc_channel_args* args,
    grpc_channel_args** /*new_args*/) {
  return grpc_local_channel_security_connector_create(
      this->Ref(), std::move(request_metadata_creds), args, target_name);
}

grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_local_server_credentials::create_security_connector() {
  return grpc_local_server_security_connector_create(this->Ref());
}

grpc_local_credentials::grpc_local_credentials(
    grpc_local_connect_type connect_type)
    : grpc_channel_credentials(GRPC_CREDENTIALS_TYPE_LOCAL),
      connect_type_(connect_type) {}

grpc_channel_credentials* grpc_local_credentials_create(
    grpc_local_connect_type connect_type) {
  return new grpc_local_credentials(connect_type);
}

grpc_local_server_credentials::grpc_local_server_credentials(
    grpc_local_connect_type connect_type)
    : grpc_server_credentials(GRPC_CREDENTIALS_TYPE_LOCAL),
      connect_type_(connect_type) {}

grpc_server_credentials* grpc_local_server_credentials_create(
    grpc_local_connect_type connect_type) {
  return new grpc_local_server_credentials(connect_type);
}
