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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_CREDENTIALS_H

#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/jwt/json_token.h"

typedef struct {
  grpc_call_credentials base;

  // Have a simple cache for now with just 1 entry. We could have a map based on
  // the service_url for a more sophisticated one.
  gpr_mu cache_mu;
  struct {
    grpc_mdelem jwt_md;
    char *service_url;
    gpr_timespec jwt_expiration;
  } cached;

  grpc_auth_json_key key;
  gpr_timespec jwt_lifetime;
} grpc_service_account_jwt_access_credentials;

// Private constructor for jwt credentials from an already parsed json key.
// Takes ownership of the key.
grpc_call_credentials *
grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
    grpc_exec_ctx *exec_ctx, grpc_auth_json_key key,
    gpr_timespec token_lifetime);

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_CREDENTIALS_H */
