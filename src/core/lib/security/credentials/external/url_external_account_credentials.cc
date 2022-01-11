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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

namespace grpc_core {

RefCountedPtr<UrlExternalAccountCredentials>
UrlExternalAccountCredentials::Create(Options options,
                                      std::vector<std::string> scopes,
                                      grpc_error_handle* error) {
  auto creds = MakeRefCounted<UrlExternalAccountCredentials>(
      std::move(options), std::move(scopes), error);
  if (*error == GRPC_ERROR_NONE) {
    return creds;
  } else {
    return nullptr;
  }
}

UrlExternalAccountCredentials::UrlExternalAccountCredentials(
    Options options, std::vector<std::string> scopes, grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes)) {
  auto it = options.credential_source.object_value().find("url");
  if (it == options.credential_source.object_value().end()) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("url field not present.");
    return;
  }
  if (it->second.type() != Json::Type::STRING) {
    *error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("url field must be a string.");
    return;
  }
  absl::StatusOr<URI> tmp_url = URI::Parse(it->second.string_value());
  if (!tmp_url.ok()) {
    *error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrFormat("Invalid credential source url. Error: %s",
                        tmp_url.status().ToString()));
    return;
  }
  url_ = *tmp_url;
  // The url must follow the format of <scheme>://<authority>/<path>
  std::vector<absl::string_view> v =
      absl::StrSplit(it->second.string_value(), absl::MaxSplits('/', 3));
  url_full_path_ = absl::StrCat("/", v[3]);
  it = options.credential_source.object_value().find("headers");
  if (it != options.credential_source.object_value().end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "The JSON value of credential source headers is not an object.");
      return;
    }
    for (auto const& header : it->second.object_value()) {
      headers_[header.first] = header.second.string_value();
    }
  }
  it = options.credential_source.object_value().find("format");
  if (it != options.credential_source.object_value().end()) {
    const Json& format_json = it->second;
    if (format_json.type() != Json::Type::OBJECT) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "The JSON value of credential source format is not an object.");
      return;
    }
    auto format_it = format_json.object_value().find("type");
    if (format_it == format_json.object_value().end()) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "format.type field not present.");
      return;
    }
    if (format_it->second.type() != Json::Type::STRING) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "format.type field must be a string.");
      return;
    }
    format_type_ = format_it->second.string_value();
    if (format_type_ == "json") {
      format_it = format_json.object_value().find("subject_token_field_name");
      if (format_it == format_json.object_value().end()) {
        *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "format.subject_token_field_name field must be present if the "
            "format is in Json.");
        return;
      }
      if (format_it->second.type() != Json::Type::STRING) {
        *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "format.subject_token_field_name field must be a string.");
        return;
      }
      format_subject_token_field_name_ = format_it->second.string_value();
    }
  }
}

void UrlExternalAccountCredentials::RetrieveSubjectToken(
    HTTPRequestContext* ctx, const Options& /*options*/,
    std::function<void(std::string, grpc_error_handle)> cb) {
  if (ctx == nullptr) {
    FinishRetrieveSubjectToken(
        "",
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Missing HTTPRequestContext to start subject token retrieval."));
    return;
  }
  ctx_ = ctx;
  cb_ = cb;
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = const_cast<char*>(url_.authority().c_str());
  request.http.path = gpr_strdup(url_full_path_.c_str());
  grpc_http_header* headers = nullptr;
  request.http.hdr_count = headers_.size();
  headers = static_cast<grpc_http_header*>(
      gpr_malloc(sizeof(grpc_http_header) * request.http.hdr_count));
  int i = 0;
  for (auto const& header : headers_) {
    headers[i].key = gpr_strdup(header.first.c_str());
    headers[i].value = gpr_strdup(header.second.c_str());
    ++i;
  }
  request.http.hdrs = headers;
  request.handshaker =
      url_.scheme() == "https" ? &grpc_httpcli_ssl : &grpc_httpcli_plaintext;
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  GRPC_CLOSURE_INIT(&ctx_->closure, OnRetrieveSubjectToken, this, nullptr);
  grpc_httpcli_get(ctx_->pollent, ResourceQuota::Default(), &request,
                   ctx_->deadline, &ctx_->closure, &ctx_->response);
  grpc_http_request_destroy(&request.http);
}

void UrlExternalAccountCredentials::OnRetrieveSubjectToken(
    void* arg, grpc_error_handle error) {
  UrlExternalAccountCredentials* self =
      static_cast<UrlExternalAccountCredentials*>(arg);
  self->OnRetrieveSubjectTokenInternal(GRPC_ERROR_REF(error));
}

void UrlExternalAccountCredentials::OnRetrieveSubjectTokenInternal(
    grpc_error_handle error) {
  if (error != GRPC_ERROR_NONE) {
    FinishRetrieveSubjectToken("", error);
    return;
  }
  absl::string_view response_body(ctx_->response.body,
                                  ctx_->response.body_length);
  if (format_type_ == "json") {
    grpc_error_handle error = GRPC_ERROR_NONE;
    Json response_json = Json::Parse(response_body, &error);
    if (error != GRPC_ERROR_NONE ||
        response_json.type() != Json::Type::OBJECT) {
      FinishRetrieveSubjectToken(
          "", GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "The format of response is not a valid json object."));
      return;
    }
    auto response_it =
        response_json.object_value().find(format_subject_token_field_name_);
    if (response_it == response_json.object_value().end()) {
      FinishRetrieveSubjectToken("", GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                         "Subject token field not present."));
      return;
    }
    if (response_it->second.type() != Json::Type::STRING) {
      FinishRetrieveSubjectToken("",
                                 GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                     "Subject token field must be a string."));
      return;
    }
    FinishRetrieveSubjectToken(response_it->second.string_value(), error);
    return;
  }
  FinishRetrieveSubjectToken(std::string(response_body), GRPC_ERROR_NONE);
}

void UrlExternalAccountCredentials::FinishRetrieveSubjectToken(
    std::string subject_token, grpc_error_handle error) {
  // Reset context
  ctx_ = nullptr;
  // Move object state into local variables.
  auto cb = cb_;
  cb_ = nullptr;
  // Invoke the callback.
  if (error != GRPC_ERROR_NONE) {
    cb("", error);
  } else {
    cb(subject_token, GRPC_ERROR_NONE);
  }
}

}  // namespace grpc_core
