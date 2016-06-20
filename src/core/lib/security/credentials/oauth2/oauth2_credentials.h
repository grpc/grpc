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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_OAUTH2_OAUTH2_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_OAUTH2_OAUTH2_CREDENTIALS_H

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/credentials.h"

// auth_refresh_token parsing.
typedef struct {
  const char *type;
  char *client_id;
  char *client_secret;
  char *refresh_token;
} grpc_auth_refresh_token;

/// Returns 1 if the object is valid, 0 otherwise.
int grpc_auth_refresh_token_is_valid(
    const grpc_auth_refresh_token *refresh_token);

/// Creates a refresh token object from string. Returns an invalid object if a
/// parsing error has been encountered.
grpc_auth_refresh_token grpc_auth_refresh_token_create_from_string(
    const char *json_string);

/// Creates a refresh token object from parsed json. Returns an invalid object
/// if a parsing error has been encountered.
grpc_auth_refresh_token grpc_auth_refresh_token_create_from_json(
    const grpc_json *json);

/// Destructs the object.
void grpc_auth_refresh_token_destruct(grpc_auth_refresh_token *refresh_token);

// -- Oauth2 Token Fetcher credentials --
//
//  This object is a base for credentials that need to acquire an oauth2 token
//  from an http service.

typedef void (*grpc_fetch_oauth2_func)(grpc_exec_ctx *exec_ctx,
                                       grpc_credentials_metadata_request *req,
                                       grpc_httpcli_context *http_context,
                                       grpc_polling_entity *pollent,
                                       grpc_iomgr_cb_func cb,
                                       gpr_timespec deadline);
typedef struct {
  grpc_call_credentials base;
  gpr_mu mu;
  grpc_credentials_md_store *access_token_md;
  gpr_timespec token_expiration;
  grpc_httpcli_context httpcli_context;
  grpc_fetch_oauth2_func fetch_func;
} grpc_oauth2_token_fetcher_credentials;

// Google refresh token credentials.
typedef struct {
  grpc_oauth2_token_fetcher_credentials base;
  grpc_auth_refresh_token refresh_token;
} grpc_google_refresh_token_credentials;

// Access token credentials.
typedef struct {
  grpc_call_credentials base;
  grpc_credentials_md_store *access_token_md;
} grpc_access_token_credentials;

// Private constructor for refresh token credentials from an already parsed
// refresh token. Takes ownership of the refresh token.
grpc_call_credentials *
grpc_refresh_token_credentials_create_from_auth_refresh_token(
    grpc_auth_refresh_token token);

// Exposed for testing only.
grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    const struct grpc_http_response *response,
    grpc_credentials_md_store **token_md, gpr_timespec *token_lifetime);

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_OAUTH2_OAUTH2_CREDENTIALS_H */
