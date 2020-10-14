//
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
//
#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/external/url_external_account_credentials.h"

#include "absl/strings/str_format.h"

#include "src/core/lib/security/util/json_util.h"

namespace grpc_core {

UrlExternalAccountCredentials::UrlExternalAccountCredentials(
    ExternalAccountCredentialsOptions options, std::vector<std::string> scopes)
    : ExternalAccountCredentials(options, scopes) {}

void UrlExternalAccountCredentials::RetrieveSubjectToken(
    const HTTPRequestContext* ctx,
    const ExternalAccountCredentialsOptions& options,
    std::function<void(std::string, grpc_error*)> cb) {
  if (ctx == nullptr) {
    FinishRetrieveSubjectToken(
        "",
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Missing HTTPRequestContext to start subject token retrieval."));
    return;
  }
  ctx_ = const_cast<HTTPRequestContext*>(ctx);
  cb_ = cb;
  grpc_error* error = ParseCredentialSource(options.credential_source);
  if (error != GRPC_ERROR_NONE) {
    FinishRetrieveSubjectToken("", error);
    return;
  }
  grpc_uri* uri = grpc_uri_parse(credential_source_.url.c_str(), false);
  if (uri == nullptr) {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrFormat("Invalid credential source url: %s.",
                                credential_source_.url)
                    .c_str()));
    return;
  }
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = const_cast<char*>(uri->authority);
  request.http.path = gpr_strdup(uri->path);
  grpc_http_header* headers = nullptr;
  request.http.hdr_count = credential_source_.headers.size();
  headers = static_cast<grpc_http_header*>(
      gpr_malloc(sizeof(grpc_http_header) * request.http.hdr_count));
  int i = 0;
  for (auto const& header : credential_source_.headers) {
    headers[i].key = gpr_strdup(header.first.c_str());
    headers[i].value = gpr_strdup(header.second.c_str());
    i++;
  }
  request.http.hdrs = headers;
  request.handshaker = (strcmp(uri->scheme, "https") == 0)
                           ? &grpc_httpcli_ssl
                           : &grpc_httpcli_plaintext;
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("external_account_credentials");
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  GRPC_CLOSURE_INIT(&ctx_->closure, OnRetrieveSubjectToken, this, nullptr);
  grpc_httpcli_get(ctx_->httpcli_context, ctx_->pollent, resource_quota,
                   &request, ctx_->deadline, &ctx_->closure, &ctx_->response);
  grpc_resource_quota_unref_internal(resource_quota);
  grpc_http_request_destroy(&request.http);
  grpc_uri_destroy(uri);
}

grpc_error* UrlExternalAccountCredentials::ParseCredentialSource(
    const Json& json) {
  CredentialSource credential_source = {};
  grpc_error* error = GRPC_ERROR_NONE;
  const char* url = grpc_json_get_string_property(json, "url", &error);
  if (error != GRPC_ERROR_NONE) {
    return error;
  } else {
    credential_source.url = std::string(url);
  }
  auto it = json.object_value().find("headers");
  if (it != json.object_value().end()) {
    if (it->second.type() != grpc_core::Json::Type::OBJECT) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "The JSON value of credential source headers is not an object.");
    }
    for (auto const& header : it->second.object_value()) {
      credential_source.headers[header.first] = header.second.string_value();
    }
  }
  it = json.object_value().find("format");
  if (it != json.object_value().end()) {
    if (it->second.type() != grpc_core::Json::Type::OBJECT) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "The JSON value of credential source format is not an object.");
    }
    const char* type = grpc_json_get_string_property(it->second, "type", &error);
    if (error == GRPC_ERROR_NONE) {
      credential_source.format.type = std::string(type);
    }
    const char* subject_token_field_name =
        grpc_json_get_string_property(it->second, "subject_token_field_name", &error);
    if (error == GRPC_ERROR_NONE) {
      credential_source.format.subject_token_field_name =
          std::string(subject_token_field_name);
    }
  }
  credential_source_ = credential_source;
  return GRPC_ERROR_NONE;
}

void UrlExternalAccountCredentials::OnRetrieveSubjectToken(void* arg,
                                                           grpc_error* error) {
  UrlExternalAccountCredentials* self =
      static_cast<UrlExternalAccountCredentials*>(arg);
  self->OnRetrieveSubjectTokenInternal(GRPC_ERROR_REF(error));
}

void UrlExternalAccountCredentials::OnRetrieveSubjectTokenInternal(
    grpc_error* error) {
  if (error != GRPC_ERROR_NONE) {
    FinishRetrieveSubjectToken("", error);
  } else {
    std::string subject_token;
    std::string response_body =
        std::string(ctx_->response.body, ctx_->response.body_length);
    if (credential_source_.format.type == "json") {
      grpc_error* error = GRPC_ERROR_NONE;
      Json response_json = Json::Parse(response_body, &error);
      const char* prop_value = grpc_json_get_string_property(
          response_json,
          credential_source_.format.subject_token_field_name.c_str(), &error);
      if (error != GRPC_ERROR_NONE) {
        FinishRetrieveSubjectToken("", error);
        return;
      } else {
        subject_token = std::string(prop_value);
      }
    } else {
      subject_token = response_body;
    }
    FinishRetrieveSubjectToken(subject_token, nullptr);
  }
}

void UrlExternalAccountCredentials::FinishRetrieveSubjectToken(
    std::string subject_token, grpc_error* error) {
  // Move object state into local variables.
  auto cb = cb_;
  cb_ = nullptr;
  // Invoke the callback.
  if (error != GRPC_ERROR_NONE) {
    cb("", error);
  } else {
    cb(subject_token, GRPC_ERROR_NONE);
  }
  // Reset context
  ctx_ = nullptr;
}

}  // namespace grpc_core