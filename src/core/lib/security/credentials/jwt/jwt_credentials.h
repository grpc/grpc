/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
    grpc_credentials_md_store *jwt_md;
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
