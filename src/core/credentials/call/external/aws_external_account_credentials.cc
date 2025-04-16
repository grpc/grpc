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
#include "src/core/credentials/call/external/aws_external_account_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include <map>
#include <optional>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/util/env.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/uri.h"

namespace grpc_core {

namespace {

const char* kExpectedEnvironmentId = "aws1";

const char* kRegionEnvVar = "AWS_REGION";
const char* kDefaultRegionEnvVar = "AWS_DEFAULT_REGION";
const char* kAccessKeyIdEnvVar = "AWS_ACCESS_KEY_ID";
const char* kSecretAccessKeyEnvVar = "AWS_SECRET_ACCESS_KEY";
const char* kSessionTokenEnvVar = "AWS_SESSION_TOKEN";

bool ShouldUseMetadataServer() {
  return !((GetEnv(kRegionEnvVar).has_value() ||
            GetEnv(kDefaultRegionEnvVar).has_value()) &&
           (GetEnv(kAccessKeyIdEnvVar).has_value() &&
            GetEnv(kSecretAccessKeyEnvVar).has_value()));
}

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

}  // namespace

//
// AwsExternalAccountCredentials::AwsFetchBody
//

AwsExternalAccountCredentials::AwsFetchBody::AwsFetchBody(
    absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done,
    AwsExternalAccountCredentials* creds, Timestamp deadline)
    : FetchBody(std::move(on_done)), creds_(creds), deadline_(deadline) {
  MutexLock lock(&mu_);
  // Do a quick async hop here, so that we can invoke the callback at
  // any time without deadlocking.
  fetch_body_ = MakeOrphanable<NoOpFetchBody>(
      creds->event_engine(),
      [self = RefAsSubclass<AwsFetchBody>()](
          absl::StatusOr<std::string> /*result*/) { self->Start(); },
      "");
}

void AwsExternalAccountCredentials::AwsFetchBody::Shutdown() {
  MutexLock lock(&mu_);
  fetch_body_.reset();
}

void AwsExternalAccountCredentials::AwsFetchBody::AsyncFinish(
    absl::StatusOr<std::string> result) {
  creds_->event_engine().Run(
      [this, self = Ref(), result = std::move(result)]() mutable {
        ExecCtx exec_ctx;
        Finish(std::move(result));
        self.reset();
      });
}

bool AwsExternalAccountCredentials::AwsFetchBody::MaybeFail(
    absl::Status status) {
  if (!status.ok()) {
    AsyncFinish(std::move(status));
    return true;
  }
  if (fetch_body_ == nullptr) {
    AsyncFinish(
        absl::CancelledError("external account credentials fetch cancelled"));
    return true;
  }
  return false;
}

void AwsExternalAccountCredentials::AwsFetchBody::Start() {
  MutexLock lock(&mu_);
  if (MaybeFail(absl::OkStatus())) return;
  if (!creds_->imdsv2_session_token_url_.empty() && ShouldUseMetadataServer()) {
    RetrieveImdsV2SessionToken();
  } else if (creds_->signer_ != nullptr) {
    BuildSubjectToken();
  } else {
    RetrieveRegion();
  }
}

void AwsExternalAccountCredentials::AwsFetchBody::RetrieveImdsV2SessionToken() {
  absl::StatusOr<URI> uri = URI::Parse(creds_->imdsv2_session_token_url_);
  if (!uri.ok()) {
    AsyncFinish(uri.status());
    return;
  }
  fetch_body_ = MakeOrphanable<HttpFetchBody>(
      [&](grpc_http_response* response, grpc_closure* on_http_response) {
        grpc_http_header* headers = static_cast<grpc_http_header*>(
            gpr_malloc(sizeof(grpc_http_header)));
        headers[0].key = gpr_strdup("x-aws-ec2-metadata-token-ttl-seconds");
        headers[0].value = gpr_strdup("300");
        grpc_http_request request;
        memset(&request, 0, sizeof(grpc_http_request));
        request.hdr_count = 1;
        request.hdrs = headers;
        RefCountedPtr<grpc_channel_credentials> http_request_creds;
        if (uri->scheme() == "http") {
          http_request_creds = RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create());
        } else {
          http_request_creds = CreateHttpRequestSSLCredentials();
        }
        auto http_request = HttpRequest::Put(
            std::move(*uri), /*args=*/nullptr, creds_->pollent(), &request,
            deadline_, on_http_response, response,
            std::move(http_request_creds));
        http_request->Start();
        grpc_http_request_destroy(&request);
        return http_request;
      },
      [self =
           RefAsSubclass<AwsFetchBody>()](absl::StatusOr<std::string> result) {
        MutexLock lock(&self->mu_);
        if (self->MaybeFail(result.status())) return;
        self->imdsv2_session_token_ = std::move(*result);
        if (self->creds_->signer_ != nullptr) {
          self->BuildSubjectToken();
        } else {
          self->RetrieveRegion();
        }
      });
}

void AwsExternalAccountCredentials::AwsFetchBody::RetrieveRegion() {
  auto region_from_env = GetEnv(kRegionEnvVar);
  if (!region_from_env.has_value()) {
    region_from_env = GetEnv(kDefaultRegionEnvVar);
  }
  if (region_from_env.has_value()) {
    region_ = std::move(*region_from_env);
    if (creds_->url_.empty()) {
      RetrieveSigningKeys();
    } else {
      RetrieveRoleName();
    }
    return;
  }
  absl::StatusOr<URI> uri = URI::Parse(creds_->region_url_);
  if (!uri.ok()) {
    AsyncFinish(GRPC_ERROR_CREATE(
        absl::StrFormat("Invalid region url. %s", uri.status().ToString())));
    return;
  }
  fetch_body_ = MakeOrphanable<HttpFetchBody>(
      [&](grpc_http_response* response, grpc_closure* on_http_response) {
        grpc_http_request request;
        memset(&request, 0, sizeof(grpc_http_request));
        AddMetadataRequestHeaders(&request);
        RefCountedPtr<grpc_channel_credentials> http_request_creds;
        if (uri->scheme() == "http") {
          http_request_creds = RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create());
        } else {
          http_request_creds = CreateHttpRequestSSLCredentials();
        }
        auto http_request = HttpRequest::Get(
            std::move(*uri), /*args=*/nullptr, creds_->pollent(), &request,
            deadline_, on_http_response, response,
            std::move(http_request_creds));
        http_request->Start();
        grpc_http_request_destroy(&request);
        return http_request;
      },
      [self =
           RefAsSubclass<AwsFetchBody>()](absl::StatusOr<std::string> result) {
        MutexLock lock(&self->mu_);
        if (self->MaybeFail(result.status())) return;
        // Remove the last letter of availability zone to get pure region
        self->region_ = result->substr(0, result->size() - 1);
        if (self->creds_->url_.empty()) {
          self->RetrieveSigningKeys();
        } else {
          self->RetrieveRoleName();
        }
      });
}

void AwsExternalAccountCredentials::AwsFetchBody::RetrieveRoleName() {
  absl::StatusOr<URI> uri = URI::Parse(creds_->url_);
  if (!uri.ok()) {
    AsyncFinish(GRPC_ERROR_CREATE(
        absl::StrFormat("Invalid url: %s.", uri.status().ToString())));
    return;
  }
  fetch_body_ = MakeOrphanable<HttpFetchBody>(
      [&](grpc_http_response* response, grpc_closure* on_http_response) {
        grpc_http_request request;
        memset(&request, 0, sizeof(grpc_http_request));
        AddMetadataRequestHeaders(&request);
        // TODO(ctiller): use the caller's resource quota.
        RefCountedPtr<grpc_channel_credentials> http_request_creds;
        if (uri->scheme() == "http") {
          http_request_creds = RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create());
        } else {
          http_request_creds = CreateHttpRequestSSLCredentials();
        }
        auto http_request = HttpRequest::Get(
            std::move(*uri), /*args=*/nullptr, creds_->pollent(), &request,
            deadline_, on_http_response, response,
            std::move(http_request_creds));
        http_request->Start();
        grpc_http_request_destroy(&request);
        return http_request;
      },
      [self =
           RefAsSubclass<AwsFetchBody>()](absl::StatusOr<std::string> result) {
        MutexLock lock(&self->mu_);
        if (self->MaybeFail(result.status())) return;
        self->role_name_ = std::move(*result);
        self->RetrieveSigningKeys();
      });
}

void AwsExternalAccountCredentials::AwsFetchBody::RetrieveSigningKeys() {
  auto access_key_id_from_env = GetEnv(kAccessKeyIdEnvVar);
  auto secret_access_key_from_env = GetEnv(kSecretAccessKeyEnvVar);
  auto token_from_env = GetEnv(kSessionTokenEnvVar);
  if (access_key_id_from_env.has_value() &&
      secret_access_key_from_env.has_value()) {
    access_key_id_ = std::move(*access_key_id_from_env);
    secret_access_key_ = std::move(*secret_access_key_from_env);
    if (token_from_env.has_value()) {
      token_ = std::move(*token_from_env);
    }
    BuildSubjectToken();
    return;
  }
  if (role_name_.empty()) {
    AsyncFinish(
        GRPC_ERROR_CREATE("Missing role name when retrieving signing keys."));
    return;
  }
  std::string url_with_role_name = absl::StrCat(creds_->url_, "/", role_name_);
  absl::StatusOr<URI> uri = URI::Parse(url_with_role_name);
  if (!uri.ok()) {
    AsyncFinish(GRPC_ERROR_CREATE(absl::StrFormat(
        "Invalid url with role name: %s.", uri.status().ToString())));
    return;
  }
  fetch_body_ = MakeOrphanable<HttpFetchBody>(
      [&](grpc_http_response* response, grpc_closure* on_http_response) {
        grpc_http_request request;
        memset(&request, 0, sizeof(grpc_http_request));
        AddMetadataRequestHeaders(&request);
        // TODO(ctiller): use the caller's resource quota.
        RefCountedPtr<grpc_channel_credentials> http_request_creds;
        if (uri->scheme() == "http") {
          http_request_creds = RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create());
        } else {
          http_request_creds = CreateHttpRequestSSLCredentials();
        }
        auto http_request = HttpRequest::Get(
            std::move(*uri), /*args=*/nullptr, creds_->pollent(), &request,
            deadline_, on_http_response, response,
            std::move(http_request_creds));
        http_request->Start();
        grpc_http_request_destroy(&request);
        return http_request;
      },
      [self =
           RefAsSubclass<AwsFetchBody>()](absl::StatusOr<std::string> result) {
        MutexLock lock(&self->mu_);
        if (self->MaybeFail(result.status())) return;
        self->OnRetrieveSigningKeys(std::move(*result));
      });
}

void AwsExternalAccountCredentials::AwsFetchBody::OnRetrieveSigningKeys(
    std::string result) {
  auto json = JsonParse(result);
  if (!json.ok()) {
    AsyncFinish(GRPC_ERROR_CREATE(absl::StrCat(
        "Invalid retrieve signing keys response: ", json.status().ToString())));
    return;
  }
  if (json->type() != Json::Type::kObject) {
    AsyncFinish(
        GRPC_ERROR_CREATE("Invalid retrieve signing keys response: "
                          "JSON type is not object"));
    return;
  }
  auto it = json->object().find("AccessKeyId");
  if (it != json->object().end() && it->second.type() == Json::Type::kString) {
    access_key_id_ = it->second.string();
  } else {
    AsyncFinish(GRPC_ERROR_CREATE(
        absl::StrFormat("Missing or invalid AccessKeyId in %s.", result)));
    return;
  }
  it = json->object().find("SecretAccessKey");
  if (it != json->object().end() && it->second.type() == Json::Type::kString) {
    secret_access_key_ = it->second.string();
  } else {
    AsyncFinish(GRPC_ERROR_CREATE(
        absl::StrFormat("Missing or invalid SecretAccessKey in %s.", result)));
    return;
  }
  it = json->object().find("Token");
  if (it != json->object().end() && it->second.type() == Json::Type::kString) {
    token_ = it->second.string();
  } else {
    AsyncFinish(GRPC_ERROR_CREATE(
        absl::StrFormat("Missing or invalid Token in %s.", result)));
    return;
  }
  BuildSubjectToken();
}

void AwsExternalAccountCredentials::AwsFetchBody::BuildSubjectToken() {
  grpc_error_handle error;
  if (creds_->signer_ == nullptr) {
    creds_->cred_verification_url_ = absl::StrReplaceAll(
        creds_->regional_cred_verification_url_, {{"{region}", region_}});
    creds_->signer_ = std::make_unique<AwsRequestSigner>(
        access_key_id_, secret_access_key_, token_, "POST",
        creds_->cred_verification_url_, region_, "",
        std::map<std::string, std::string>(), &error);
    if (!error.ok()) {
      AsyncFinish(GRPC_ERROR_CREATE_REFERENCING(
          "Creating aws request signer failed.", &error, 1));
      return;
    }
  }
  auto signed_headers = creds_->signer_->GetSignedRequestHeaders();
  if (!error.ok()) {
    AsyncFinish(GRPC_ERROR_CREATE_REFERENCING(
        "Invalid getting signed request headers.", &error, 1));
    return;
  }
  // Construct subject token
  Json::Array headers;
  headers.push_back(Json::FromObject(
      {{"key", Json::FromString("Authorization")},
       {"value", Json::FromString(signed_headers["Authorization"])}}));
  headers.push_back(
      Json::FromObject({{"key", Json::FromString("host")},
                        {"value", Json::FromString(signed_headers["host"])}}));
  headers.push_back(Json::FromObject(
      {{"key", Json::FromString("x-amz-date")},
       {"value", Json::FromString(signed_headers["x-amz-date"])}}));
  headers.push_back(Json::FromObject(
      {{"key", Json::FromString("x-amz-security-token")},
       {"value", Json::FromString(signed_headers["x-amz-security-token"])}}));
  headers.push_back(Json::FromObject(
      {{"key", Json::FromString("x-goog-cloud-target-resource")},
       {"value", Json::FromString(creds_->audience_)}}));
  Json subject_token_json = Json::FromObject(
      {{"url", Json::FromString(creds_->cred_verification_url_)},
       {"method", Json::FromString("POST")},
       {"headers", Json::FromArray(headers)}});
  std::string subject_token = UrlEncode(JsonDump(subject_token_json));
  AsyncFinish(std::move(subject_token));
}

void AwsExternalAccountCredentials::AwsFetchBody::AddMetadataRequestHeaders(
    grpc_http_request* request) {
  if (!imdsv2_session_token_.empty()) {
    CHECK_EQ(request->hdr_count, 0u);
    CHECK_EQ(request->hdrs, nullptr);
    grpc_http_header* headers =
        static_cast<grpc_http_header*>(gpr_malloc(sizeof(grpc_http_header)));
    headers[0].key = gpr_strdup("x-aws-ec2-metadata-token");
    headers[0].value = gpr_strdup(imdsv2_session_token_.c_str());
    request->hdr_count = 1;
    request->hdrs = headers;
  }
}

//
// AwsExternalAccountCredentials
//

absl::StatusOr<RefCountedPtr<AwsExternalAccountCredentials>>
AwsExternalAccountCredentials::Create(
    Options options, std::vector<std::string> scopes,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine) {
  grpc_error_handle error;
  auto creds = MakeRefCounted<AwsExternalAccountCredentials>(
      std::move(options), std::move(scopes), std::move(event_engine), &error);
  if (!error.ok()) return error;
  return creds;
}

AwsExternalAccountCredentials::AwsExternalAccountCredentials(
    Options options, std::vector<std::string> scopes,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes),
                                 std::move(event_engine)) {
  audience_ = options.audience;
  auto it = options.credential_source.object().find("environment_id");
  if (it == options.credential_source.object().end()) {
    *error = GRPC_ERROR_CREATE("environment_id field not present.");
    return;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("environment_id field must be a string.");
    return;
  }
  if (it->second.string() != kExpectedEnvironmentId) {
    *error = GRPC_ERROR_CREATE("environment_id does not match.");
    return;
  }
  it = options.credential_source.object().find("region_url");
  if (it == options.credential_source.object().end()) {
    *error = GRPC_ERROR_CREATE("region_url field not present.");
    return;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("region_url field must be a string.");
    return;
  }
  region_url_ = it->second.string();
  it = options.credential_source.object().find("url");
  if (it != options.credential_source.object().end() &&
      it->second.type() == Json::Type::kString) {
    url_ = it->second.string();
  }
  it =
      options.credential_source.object().find("regional_cred_verification_url");
  if (it == options.credential_source.object().end()) {
    *error =
        GRPC_ERROR_CREATE("regional_cred_verification_url field not present.");
    return;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE(
        "regional_cred_verification_url field must be a string.");
    return;
  }
  regional_cred_verification_url_ = it->second.string();
  it = options.credential_source.object().find("imdsv2_session_token_url");
  if (it != options.credential_source.object().end() &&
      it->second.type() == Json::Type::kString) {
    imdsv2_session_token_url_ = it->second.string();
  }
}

std::string AwsExternalAccountCredentials::debug_string() {
  return absl::StrCat("AwsExternalAccountCredentials{Audience:", audience(),
                      ")");
}

UniqueTypeName AwsExternalAccountCredentials::Type() {
  static UniqueTypeName::Factory kFactory("AwsExternalAccountCredentials");
  return kFactory.Create();
}

OrphanablePtr<ExternalAccountCredentials::FetchBody>
AwsExternalAccountCredentials::RetrieveSubjectToken(
    Timestamp deadline,
    absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done) {
  return MakeOrphanable<AwsFetchBody>(std::move(on_done), this, deadline);
}

absl::string_view AwsExternalAccountCredentials::CredentialSourceType() {
  return "aws";
}

}  // namespace grpc_core
