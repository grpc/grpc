
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

bool IsTokenExchangeRequestValid(const TokenExchangeRequest* request) {
  if (request == nullptr) {
    return false;
  }
  if (request->grant_type.empty()) {
    return false;
  }
  if (request->subject_token.empty()) {
    return false;
  }
  if (request->subject_token_type.empty()) {
    return false;
  }
  if (request->actor_token.empty() != request->actor_token_type.empty()) {
    return false;
  }
  return true;
}

grpc_httpcli_request* GenerateHttpCliRequest(
    const std::string token_url, const TokenExchangeRequest* request,
    const ClientAuthentication* client_auth) {
  grpc_httpcli_request* result = static_cast<grpc_httpcli_request*>(
      gpr_malloc(sizeof(grpc_httpcli_request)));
  memset(result, 0, sizeof(*result));
  if (!IsTokenExchangeRequestValid(request)) {
    return result;
  }
  grpc_uri* uri = grpc_uri_parse(token_url, false);
  result->host = gpr_strdup(uri->authority);
  result->http.path = gpr_strdup(uri->path);
  result->handshaker = (strcmp(uri->scheme, "https") == 0)
                           ? &grpc_httpcli_ssl
                           : &grpc_httpcli_plaintext;
  gpr_free(uri);
  grpc_http_header* headers = nullptr;
  if (client_auth != nullptr &&
      client_auth->client_type ==
          ClientAuthentication::ConfidentialClientType::kBasic) {
    result->http.hdr_count = 2;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * 2));
    headers[0].key = gpr_strdup("Content-Type");
    headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
    std::string raw_cred = absl::StrFormat("%s:%s", client_auth->client_id,
                                           client_auth->client_secret);
    char* encoded_cred =
        grpc_base64_encode(raw_cred.c_str(), raw_cred.length(), 0, 0);
    std::string str = absl::StrFormat("Basic %s", std::string(encoded_cred));
    headers[1].key = gpr_strdup("Authorization");
    headers[1].value = gpr_strdup(str.c_str());
    gpr_free(encoded_cred);
  } else if (client_auth != nullptr &&
             client_auth->client_type ==
                 ClientAuthentication::ConfidentialClientType::kRequestBody) {
    result->http.hdr_count = 1;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * 1));
    headers[0].key = gpr_strdup("Content-Type");
    headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
  }
  result->http.hdrs = headers;
  return result;
}

std::string GenerateHttpCliRequestBody(
    const std::string token_url, const TokenExchangeRequest* request,
    const ClientAuthentication* client_auth) {
  std::string result;
  if (!IsTokenExchangeRequestValid(request)) {
    return result;
  }
  std::vector<std::string> body_parts;
  if (!request->grant_type.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "grant_type", request->grant_type));
  }
  if (!request->resource.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "resource", request->resource));
  }
  if (!request->audience.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "audience", request->audience));
  }
  if (!request->scope.empty()) {
    body_parts.push_back(absl::StrFormat("%s=%s", "scope", request->scope));
  }
  if (!request->requested_token_type.empty()) {
    body_parts.push_back(absl::StrFormat("%s=%s", "requested_token_type",
                                         request->requested_token_type));
  }
  if (!request->subject_token.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "subject_token", request->subject_token));
  }
  if (!request->subject_token_type.empty()) {
    body_parts.push_back(absl::StrFormat("%s=%s", "subject_token_type",
                                         request->subject_token_type));
  }
  if (!request->actor_token.empty()) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "actor_token", request->actor_token));
  }
  if (!request->actor_token_type.empty()) {
    body_parts.push_back(absl::StrFormat("%s=%s", "actor_token_type",
                                         request->actor_token_type));
  }
  if (client_auth != nullptr &&
      client_auth->client_type ==
          ClientAuthentication::ConfidentialClientType::kRequestBody) {
    body_parts.push_back(
        absl::StrFormat("%s=%s", "client_id", client_auth->client_id));
    body_parts.push_back(
        absl::StrFormat("%s=%s", "client_secret", client_auth->client_secret));
  }
  result = absl::StrJoin(body_parts, "&");
  return result;
}

}  // namespace grpc_core
