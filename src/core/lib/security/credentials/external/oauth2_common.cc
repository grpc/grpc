
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

#include "oauth2_common.h"

#include <iostream>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "src/core/lib/slice/b64.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

bool TokenExchangeRequest::isValid() {
  if (this->grantType.empty()) {
    return false;
  }
  if (this->subjectToken.empty()) {
    return false;
  }
  if (this->subjectTokenType.empty()) {
    return false;
  }
  if (this->actorToken.empty() != this->actorTokenType.empty()) {
    return false;
  }
  return true;
}

grpc_httpcli_request TokenExchangeRequest::generateHttpRequest(
    const std::string tokenUrl, const ClientAuthentication* clientAuth) {
  grpc_httpcli_request result;
  memset(&result, 0, sizeof(grpc_httpcli_request));
  if (!this->isValid()) {
    return result;
  }

  grpc_uri* uri = grpc_uri_parse(tokenUrl, false);
  result.host = (char*)uri->authority;
  result.http.path = (char*)uri->path;
  result.handshaker = (strcmp(uri->scheme, "https") == 0)
                          ? &grpc_httpcli_ssl
                          : &grpc_httpcli_plaintext;
  grpc_http_header* headers = nullptr;
  if (clientAuth != nullptr &&
      clientAuth->clientType == ClientAuthentication::basic) {
    result.http.hdr_count = 2;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * 2));
    headers[0].key = gpr_strdup(const_cast<char*>("Content-Type"));
    headers[0].value =
        gpr_strdup(const_cast<char*>("application/x-www-form-urlencoded"));
    std::string rawCred = absl::StrFormat("%s:%s", clientAuth->clientId,
                                          clientAuth->clientSecret);
    char* encodedCred =
        grpc_base64_encode(rawCred.c_str(), rawCred.length(), 0, 0);
    std::string str = absl::StrFormat("Basic %s", std::string(encodedCred));
    gpr_free(encodedCred);
    headers[1].key = gpr_strdup(const_cast<char*>("Authorization"));
    char* cstr = new char[str.length() + 1];
    strcpy(cstr, str.c_str());
    headers[1].value = gpr_strdup(const_cast<char*>(cstr));
    gpr_free(cstr);
  } else {
    result.http.hdr_count = 1;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * 1));
    headers[0].key = gpr_strdup(const_cast<char*>("Content-Type"));
    headers[0].value =
        gpr_strdup(const_cast<char*>("application/x-www-form-urlencoded"));
  }
  result.http.hdrs = headers;
  return result;
}

std::string TokenExchangeRequest::generateHttpRequestBody(
    const std::string tokenUrl, const ClientAuthentication* clientAuth) {
  std::string result;
  if (!this->isValid()) {
    return result;
  }
  std::vector<std::string> body_parts;
  if (!this->grantType.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "grant_type", this->grantType));
  }
  if (!this->resource.empty()) {
    body_parts.push_back(absl::StrFormat("%s=%s", "resource", this->resource));
  }
  if (!this->audience.empty()) {
    body_parts.push_back(absl::StrFormat("%s=%s", "audience", this->audience));
  }
  if (!this->scope.empty()) {
    body_parts.push_back(absl::StrFormat("%s=%s", "scope", this->scope));
  }
  if (!this->requestedTokenType.empty()) {
    body_parts.push_back(absl::StrFormat("%s=%s", "requested_token_type",
                                         this->requestedTokenType));
  }
  if (!this->subjectToken.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "subject_token", this->subjectToken));
  }
  if (!this->subjectTokenType.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "subject_token_type", this->subjectTokenType));
  }
  if (!this->actorToken.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "actor_token", this->actorToken));
  }
  if (!this->actorTokenType.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "actor_token_type", this->actorTokenType));
  }
  if (clientAuth != nullptr &&
      clientAuth->clientType == ClientAuthentication::requestBody) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "client_id", clientAuth->clientId));
    body_parts.push_back(
        absl::StrFormat("%s=%s", "client_secret", clientAuth->clientSecret));
  }
  result = absl::StrJoin(body_parts, "&");
  return result;
}

}  // namespace grpc_core
