/*
 *
 * Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_OAUTH2_COMMON_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_OAUTH2_COMMON_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/credentials.h"

#include <string>

/**
 * Defines the client authentication credentials for basic and request-body
 * types.
 * Based on https://tools.ietf.org/html/rfc6749#section-2.3.1
 */
typedef struct {
  enum grpc_oauth_confidential_client_type { basic, request_body };

  grpc_oauth_confidential_client_type client_type;
  std::string client_id;
  std::string client_secret;
} grpc_oauth2_client_authentication;

/**
 * Defines the OAuth 2.0 token exchange request based on
 * https://tools.ietf.org/html/rfc8693#section-2.2.1
 */
typedef struct {
  std::string grant_type;
  std::string resource;
  std::string audience;
  std::string scope;
  std::string requested_token_type;
  std::string subject_token;
  std::string subject_token_type;
  std::string actor_token;
  std::string actor_token_type;
} grpc_oauth2_token_exchange_request;

/**
 * Returns true if the request is valid, false otherwise.
 */
bool grpc_oauth2_token_exchange_request_is_valid(
    const grpc_oauth2_token_exchange_request* request);

/**
 * Build the http request with token url, sts request and client auth.
 */
grpc_httpcli_request grpc_generate_token_exchange_request(
    const std::string token_url,
    const grpc_oauth2_token_exchange_request* request,
    const grpc_oauth2_client_authentication* client_auth);

/**
 * Build the http request body with token url, sts request and client auth.
 */
std::string grpc_generate_token_exchange_request_body(
    const std::string token_url,
    const grpc_oauth2_token_exchange_request* request,
    const grpc_oauth2_client_authentication* client_auth);

/**
 * Defines the OAuth 2.0 token exchange response based on
 * https://tools.ietf.org/html/rfc8693#section-2.2.1
 */
typedef struct {
  std::string access_token;
  std::string issued_token_type;
  std::string token_type;
  std::string expires_in;
  std::string refresh_token;
  std::string scope;
} grpc_oauth2_token_exchange_response;

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_OAUTH2_COMMON_H
