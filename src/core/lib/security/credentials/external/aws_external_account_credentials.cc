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

#include "src/core/lib/security/credentials/external/aws_external_account_credentials.h"

#include <string.h>

#include <map>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/http/httpcli_ssl_credentials.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

namespace {

const char* awsEc2MetadataIpv4Address = "169.254.169.254";
const char* awsEc2MetadataIpv6Address = "fd00:ec2::254";

const char* kExpectedEnvironmentId = "aws1";

const char* kRegionEnvVar = "AWS_REGION";
const char* kDefaultRegionEnvVar = "AWS_DEFAULT_REGION";
const char* kAccessKeyIdEnvVar = "AWS_ACCESS_KEY_ID";
const char* kSecretAccessKeyEnvVar = "AWS_SECRET_ACCESS_KEY";
const char* kSessionTokenEnvVar = "AWS_SESSION_TOKEN";

std::string UrlEncode(const absl::string_view& s) {
  const char* hex = "0123456789ABCDEF";
  std::string result;
  result.reserve(s.length());
  for (auto c : s) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '!' ||
        c == '\'' || c == '(' || c == ')' || c == '*' || c == '~' || c == '.') {
      result.push_back(c);
    } else {
      result.push_back('%');
      result.push_back(hex[static_cast<unsigned char>(c) >> 4]);
      result.push_back(hex[static_cast<unsigned char>(c) & 15]);
    }
  }
  return result;
}

bool ValidateAwsUrl(const std::string& urlString) {
  absl::StatusOr<URI> url = URI::Parse(urlString);
  if (!url.ok()) return false;
  absl::string_view host;
  absl::string_view port;
  SplitHostPort(url->authority(), &host, &port);
  return host == awsEc2MetadataIpv4Address || host == awsEc2MetadataIpv6Address;
}

}  // namespace

RefCountedPtr<AwsExternalAccountCredentials>
AwsExternalAccountCredentials::Create(Options options,
                                      std::vector<std::string> scopes,
                                      grpc_error_handle* error) {
  auto creds = MakeRefCounted<AwsExternalAccountCredentials>(
      std::move(options), std::move(scopes), error);
  if (error->ok()) {
    return creds;
  } else {
    return nullptr;
  }
}

AwsExternalAccountCredentials::AwsExternalAccountCredentials(
    Options options, std::vector<std::string> scopes, grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes)) {
  audience_ = options.audience;
  auto it = options.credential_source.object_value().find("environment_id");
  if (it == options.credential_source.object_value().end()) {
    *error = GRPC_ERROR_CREATE("environment_id field not present.");
    return;
  }
  if (it->second.type() != Json::Type::STRING) {
    *error = GRPC_ERROR_CREATE("environment_id field must be a string.");
    return;
  }
  if (it->second.string_value() != kExpectedEnvironmentId) {
    *error = GRPC_ERROR_CREATE("environment_id does not match.");
    return;
  }
  it = options.credential_source.object_value().find("region_url");
  if (it == options.credential_source.object_value().end()) {
    *error = GRPC_ERROR_CREATE("region_url field not present.");
    return;
  }
  if (it->second.type() != Json::Type::STRING) {
    *error = GRPC_ERROR_CREATE("region_url field must be a string.");
    return;
  }
  region_url_ = it->second.string_value();
  if (!ValidateAwsUrl(region_url_)) {
    *error = GRPC_ERROR_CREATE(absl::StrFormat(
        "Invalid host for region_url field, expecting %s or %s.",
        awsEc2MetadataIpv4Address, awsEc2MetadataIpv6Address));
    return;
  }
  it = options.credential_source.object_value().find("url");
  if (it != options.credential_source.object_value().end() &&
      it->second.type() == Json::Type::STRING) {
    url_ = it->second.string_value();
    if (!ValidateAwsUrl(url_)) {
      *error = GRPC_ERROR_CREATE(absl::StrFormat(
          "Invalid host for url field, expecting %s or %s.",
          awsEc2MetadataIpv4Address, awsEc2MetadataIpv6Address));
      return;
    }
  }
  it = options.credential_source.object_value().find(
      "regional_cred_verification_url");
  if (it == options.credential_source.object_value().end()) {
    *error =
        GRPC_ERROR_CREATE("regional_cred_verification_url field not present.");
    return;
  }
  if (it->second.type() != Json::Type::STRING) {
    *error = GRPC_ERROR_CREATE(
        "regional_cred_verification_url field must be a string.");
    return;
  }
  regional_cred_verification_url_ = it->second.string_value();
  it =
      options.credential_source.object_value().find("imdsv2_session_token_url");
  if (it != options.credential_source.object_value().end() &&
      it->second.type() == Json::Type::STRING) {
    imdsv2_session_token_url_ = it->second.string_value();
    if (!ValidateAwsUrl(imdsv2_session_token_url_)) {
      *error = GRPC_ERROR_CREATE(absl::StrFormat(
          "Invalid host for imdsv2_session_token_url field, expecting %s or "
          "%s.",
          awsEc2MetadataIpv4Address, awsEc2MetadataIpv6Address));
      return;
    }
  }
}

void AwsExternalAccountCredentials::RetrieveSubjectToken(
    HTTPRequestContext* ctx, const Options& /*options*/,
    std::function<void(std::string, grpc_error_handle)> cb) {
  if (ctx == nullptr) {
    FinishRetrieveSubjectToken(
        "",
        GRPC_ERROR_CREATE(
            "Missing HTTPRequestContext to start subject token retrieval."));
    return;
  }
  ctx_ = ctx;
  cb_ = cb;
  if (!imdsv2_session_token_url_.empty()) {
    RetrieveImdsV2SessionToken();
  } else if (signer_ != nullptr) {
    BuildSubjectToken();
  } else {
    RetrieveRegion();
  }
}

void AwsExternalAccountCredentials::RetrieveImdsV2SessionToken() {
  absl::StatusOr<URI> uri = URI::Parse(imdsv2_session_token_url_);
  if (!uri.ok()) {
    return;
  }
  grpc_http_header* headers =
      static_cast<grpc_http_header*>(gpr_malloc(sizeof(grpc_http_header)));
  headers[0].key = gpr_strdup("x-aws-ec2-metadata-token-ttl-seconds");
  headers[0].value = gpr_strdup("300");
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  request.hdr_count = 1;
  request.hdrs = headers;
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  GRPC_CLOSURE_INIT(&ctx_->closure, OnRetrieveImdsV2SessionToken, this,
                    nullptr);
  RefCountedPtr<grpc_channel_credentials> http_request_creds;
  if (uri->scheme() == "http") {
    http_request_creds = RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  } else {
    http_request_creds = CreateHttpRequestSSLCredentials();
  }
  http_request_ =
      HttpRequest::Put(std::move(*uri), nullptr /* channel args */,
                       ctx_->pollent, &request, ctx_->deadline, &ctx_->closure,
                       &ctx_->response, std::move(http_request_creds));
  http_request_->Start();
  grpc_http_request_destroy(&request);
}

void AwsExternalAccountCredentials::OnRetrieveImdsV2SessionToken(
    void* arg, grpc_error_handle error) {
  AwsExternalAccountCredentials* self =
      static_cast<AwsExternalAccountCredentials*>(arg);
  self->OnRetrieveImdsV2SessionTokenInternal(error);
}

void AwsExternalAccountCredentials::OnRetrieveImdsV2SessionTokenInternal(
    grpc_error_handle error) {
  if (!error.ok()) {
    FinishRetrieveSubjectToken("", error);
    return;
  }
  imdsv2_session_token_ =
      std::string(ctx_->response.body, ctx_->response.body_length);
  if (signer_ != nullptr) {
    BuildSubjectToken();
  } else {
    RetrieveRegion();
  }
}

void AwsExternalAccountCredentials::AddMetadataRequestHeaders(
    grpc_http_request* request) {
  if (!imdsv2_session_token_.empty()) {
    GPR_ASSERT(request->hdr_count == 0);
    GPR_ASSERT(request->hdrs == nullptr);
    grpc_http_header* headers =
        static_cast<grpc_http_header*>(gpr_malloc(sizeof(grpc_http_header)));
    headers[0].key = gpr_strdup("x-aws-ec2-metadata-token");
    headers[0].value = gpr_strdup(imdsv2_session_token_.c_str());
    request->hdr_count = 1;
    request->hdrs = headers;
  }
}

void AwsExternalAccountCredentials::RetrieveRegion() {
  auto region_from_env = GetEnv(kRegionEnvVar);
  if (!region_from_env.has_value()) {
    region_from_env = GetEnv(kDefaultRegionEnvVar);
  }
  if (region_from_env.has_value()) {
    region_ = std::move(*region_from_env);
    if (url_.empty()) {
      RetrieveSigningKeys();
    } else {
      RetrieveRoleName();
    }
    return;
  }
  absl::StatusOr<URI> uri = URI::Parse(region_url_);
  if (!uri.ok()) {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE(absl::StrFormat("Invalid region url. %s",
                                              uri.status().ToString())));
    return;
  }
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  AddMetadataRequestHeaders(&request);
  GRPC_CLOSURE_INIT(&ctx_->closure, OnRetrieveRegion, this, nullptr);
  RefCountedPtr<grpc_channel_credentials> http_request_creds;
  if (uri->scheme() == "http") {
    http_request_creds = RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  } else {
    http_request_creds = CreateHttpRequestSSLCredentials();
  }
  http_request_ =
      HttpRequest::Get(std::move(*uri), nullptr /* channel args */,
                       ctx_->pollent, &request, ctx_->deadline, &ctx_->closure,
                       &ctx_->response, std::move(http_request_creds));
  http_request_->Start();
  grpc_http_request_destroy(&request);
}

void AwsExternalAccountCredentials::OnRetrieveRegion(void* arg,
                                                     grpc_error_handle error) {
  AwsExternalAccountCredentials* self =
      static_cast<AwsExternalAccountCredentials*>(arg);
  self->OnRetrieveRegionInternal(error);
}

void AwsExternalAccountCredentials::OnRetrieveRegionInternal(
    grpc_error_handle error) {
  if (!error.ok()) {
    FinishRetrieveSubjectToken("", error);
    return;
  }
  // Remove the last letter of availability zone to get pure region
  absl::string_view response_body(ctx_->response.body,
                                  ctx_->response.body_length);
  region_ = std::string(response_body.substr(0, response_body.size() - 1));
  if (url_.empty()) {
    RetrieveSigningKeys();
  } else {
    RetrieveRoleName();
  }
}

void AwsExternalAccountCredentials::RetrieveRoleName() {
  absl::StatusOr<URI> uri = URI::Parse(url_);
  if (!uri.ok()) {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE(
                absl::StrFormat("Invalid url: %s.", uri.status().ToString())));
    return;
  }
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  AddMetadataRequestHeaders(&request);
  GRPC_CLOSURE_INIT(&ctx_->closure, OnRetrieveRoleName, this, nullptr);
  // TODO(ctiller): use the caller's resource quota.
  RefCountedPtr<grpc_channel_credentials> http_request_creds;
  if (uri->scheme() == "http") {
    http_request_creds = RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  } else {
    http_request_creds = CreateHttpRequestSSLCredentials();
  }
  http_request_ =
      HttpRequest::Get(std::move(*uri), nullptr /* channel args */,
                       ctx_->pollent, &request, ctx_->deadline, &ctx_->closure,
                       &ctx_->response, std::move(http_request_creds));
  http_request_->Start();
  grpc_http_request_destroy(&request);
}

void AwsExternalAccountCredentials::OnRetrieveRoleName(
    void* arg, grpc_error_handle error) {
  AwsExternalAccountCredentials* self =
      static_cast<AwsExternalAccountCredentials*>(arg);
  self->OnRetrieveRoleNameInternal(error);
}

void AwsExternalAccountCredentials::OnRetrieveRoleNameInternal(
    grpc_error_handle error) {
  if (!error.ok()) {
    FinishRetrieveSubjectToken("", error);
    return;
  }
  role_name_ = std::string(ctx_->response.body, ctx_->response.body_length);
  RetrieveSigningKeys();
}

void AwsExternalAccountCredentials::RetrieveSigningKeys() {
  auto access_key_id_from_env = GetEnv(kAccessKeyIdEnvVar);
  auto secret_access_key_from_env = GetEnv(kSecretAccessKeyEnvVar);
  auto token_from_env = GetEnv(kSessionTokenEnvVar);
  if (access_key_id_from_env.has_value() &&
      secret_access_key_from_env.has_value() && token_from_env.has_value()) {
    access_key_id_ = std::move(*access_key_id_from_env);
    secret_access_key_ = std::move(*secret_access_key_from_env);
    token_ = std::move(*token_from_env);
    BuildSubjectToken();
    return;
  }
  if (role_name_.empty()) {
    FinishRetrieveSubjectToken(
        "",
        GRPC_ERROR_CREATE("Missing role name when retrieving signing keys."));
    return;
  }
  std::string url_with_role_name = absl::StrCat(url_, "/", role_name_);
  absl::StatusOr<URI> uri = URI::Parse(url_with_role_name);
  if (!uri.ok()) {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE(absl::StrFormat("Invalid url with role name: %s.",
                                              uri.status().ToString())));
    return;
  }
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  AddMetadataRequestHeaders(&request);
  GRPC_CLOSURE_INIT(&ctx_->closure, OnRetrieveSigningKeys, this, nullptr);
  // TODO(ctiller): use the caller's resource quota.
  RefCountedPtr<grpc_channel_credentials> http_request_creds;
  if (uri->scheme() == "http") {
    http_request_creds = RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  } else {
    http_request_creds = CreateHttpRequestSSLCredentials();
  }
  http_request_ =
      HttpRequest::Get(std::move(*uri), nullptr /* channel args */,
                       ctx_->pollent, &request, ctx_->deadline, &ctx_->closure,
                       &ctx_->response, std::move(http_request_creds));
  http_request_->Start();
  grpc_http_request_destroy(&request);
}

void AwsExternalAccountCredentials::OnRetrieveSigningKeys(
    void* arg, grpc_error_handle error) {
  AwsExternalAccountCredentials* self =
      static_cast<AwsExternalAccountCredentials*>(arg);
  self->OnRetrieveSigningKeysInternal(error);
}

void AwsExternalAccountCredentials::OnRetrieveSigningKeysInternal(
    grpc_error_handle error) {
  if (!error.ok()) {
    FinishRetrieveSubjectToken("", error);
    return;
  }
  absl::string_view response_body(ctx_->response.body,
                                  ctx_->response.body_length);
  auto json = Json::Parse(response_body);
  if (!json.ok()) {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE(
                absl::StrCat("Invalid retrieve signing keys response: ",
                             json.status().ToString())));
    return;
  }
  if (json->type() != Json::Type::OBJECT) {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE("Invalid retrieve signing keys response: "
                              "JSON type is not object"));
    return;
  }
  auto it = json->object_value().find("AccessKeyId");
  if (it != json->object_value().end() &&
      it->second.type() == Json::Type::STRING) {
    access_key_id_ = it->second.string_value();
  } else {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE(absl::StrFormat(
                "Missing or invalid AccessKeyId in %s.", response_body)));
    return;
  }
  it = json->object_value().find("SecretAccessKey");
  if (it != json->object_value().end() &&
      it->second.type() == Json::Type::STRING) {
    secret_access_key_ = it->second.string_value();
  } else {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE(absl::StrFormat(
                "Missing or invalid SecretAccessKey in %s.", response_body)));
    return;
  }
  it = json->object_value().find("Token");
  if (it != json->object_value().end() &&
      it->second.type() == Json::Type::STRING) {
    token_ = it->second.string_value();
  } else {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE(absl::StrFormat("Missing or invalid Token in %s.",
                                              response_body)));
    return;
  }
  BuildSubjectToken();
}

void AwsExternalAccountCredentials::BuildSubjectToken() {
  grpc_error_handle error;
  if (signer_ == nullptr) {
    cred_verification_url_ = absl::StrReplaceAll(
        regional_cred_verification_url_, {{"{region}", region_}});
    signer_ = std::make_unique<AwsRequestSigner>(
        access_key_id_, secret_access_key_, token_, "POST",
        cred_verification_url_, region_, "",
        std::map<std::string, std::string>(), &error);
    if (!error.ok()) {
      FinishRetrieveSubjectToken(
          "", GRPC_ERROR_CREATE_REFERENCING(
                  "Creating aws request signer failed.", &error, 1));
      return;
    }
  }
  auto signed_headers = signer_->GetSignedRequestHeaders();
  if (!error.ok()) {
    FinishRetrieveSubjectToken(
        "", GRPC_ERROR_CREATE_REFERENCING("Invalid getting signed request"
                                          "headers.",
                                          &error, 1));
    return;
  }
  // Construct subject token
  Json::Array headers;
  headers.push_back(Json(
      {{"key", "Authorization"}, {"value", signed_headers["Authorization"]}}));
  headers.push_back(Json({{"key", "host"}, {"value", signed_headers["host"]}}));
  headers.push_back(
      Json({{"key", "x-amz-date"}, {"value", signed_headers["x-amz-date"]}}));
  headers.push_back(Json({{"key", "x-amz-security-token"},
                          {"value", signed_headers["x-amz-security-token"]}}));
  headers.push_back(
      Json({{"key", "x-goog-cloud-target-resource"}, {"value", audience_}}));
  Json::Object object{{"url", Json(cred_verification_url_)},
                      {"method", Json("POST")},
                      {"headers", Json(headers)}};
  Json subject_token_json(object);
  std::string subject_token = UrlEncode(subject_token_json.Dump());
  FinishRetrieveSubjectToken(subject_token, absl::OkStatus());
}

void AwsExternalAccountCredentials::FinishRetrieveSubjectToken(
    std::string subject_token, grpc_error_handle error) {
  // Reset context
  ctx_ = nullptr;
  // Move object state into local variables.
  auto cb = cb_;
  cb_ = nullptr;
  // Invoke the callback.
  if (!error.ok()) {
    cb("", error);
  } else {
    cb(subject_token, absl::OkStatus());
  }
}

}  // namespace grpc_core
