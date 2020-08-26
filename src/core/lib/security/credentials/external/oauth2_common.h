
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_OAUTH2_COMMON_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_OAUTH2_COMMON_H

#include <string>

#include <grpc/support/port_platform.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/credentials.h"

namespace grpc_core {

// Defines the client authentication credentials for basic and request-body
// types.
// Based on https://tools.ietf.org/html/rfc6749#section-2.3.1
class ClientAuthentication {
 public:
  enum ConfidentialClientType { basic, requestBody };

 public:
  ConfidentialClientType clientType;
  std::string clientId;
  std::string clientSecret;
};

// Defines the OAuth 2.0 token exchange request based on
// https://tools.ietf.org/html/rfc8693#section-2.2.1
class TokenExchangeRequest {
 public:
  std::string grantType;
  std::string resource;
  std::string audience;
  std::string scope;
  std::string requestedTokenType;
  std::string subjectToken;
  std::string subjectTokenType;
  std::string actorToken;
  std::string actorTokenType;

 public:
  // Returns true if the request is valid, false otherwise.
  bool isValid();

  // Build the http request with token url, sts request and client auth.
  grpc_httpcli_request generateHttpRequest(
      const std::string tokenUrl, const ClientAuthentication* clientAuth);

  // Build the http request body with token url, sts request and client auth.
  std::string generateHttpRequestBody(const std::string tokenTrl,
                                      const ClientAuthentication* clientAuth);
};

// Defines the OAuth 2.0 token exchange response based on
// https://tools.ietf.org/html/rfc8693#section-2.2.1
class TokenExchangeResponse {
 public:
  std::string accessToken;
  std::string issuedTokenType;
  std::string tokenType;
  std::string expiresIn;
  std::string refreshToken;
  std::string scope;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_OAUTH2_COMMON_H
