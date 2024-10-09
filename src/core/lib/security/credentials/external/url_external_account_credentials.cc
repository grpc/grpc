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
#include "src/core/lib/security/credentials/external/url_external_account_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/http_client/parser.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"

namespace grpc_core {

absl::StatusOr<RefCountedPtr<UrlExternalAccountCredentials>>
UrlExternalAccountCredentials::Create(
    Options options, std::vector<std::string> scopes,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine) {
  grpc_error_handle error;
  auto creds = MakeRefCounted<UrlExternalAccountCredentials>(
      std::move(options), std::move(scopes), std::move(event_engine), &error);
  if (!error.ok()) return error;
  return creds;
}

UrlExternalAccountCredentials::UrlExternalAccountCredentials(
    Options options, std::vector<std::string> scopes,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    grpc_error_handle* error)
    : ExternalAccountCredentials(options, std::move(scopes),
                                 std::move(event_engine)) {
  auto it = options.credential_source.object().find("url");
  if (it == options.credential_source.object().end()) {
    *error = GRPC_ERROR_CREATE("url field not present.");
    return;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("url field must be a string.");
    return;
  }
  absl::StatusOr<URI> tmp_url = URI::Parse(it->second.string());
  if (!tmp_url.ok()) {
    *error = GRPC_ERROR_CREATE(
        absl::StrFormat("Invalid credential source url. Error: %s",
                        tmp_url.status().ToString()));
    return;
  }
  url_ = *tmp_url;
  // The url must follow the format of <scheme>://<authority>/<path>
  std::vector<absl::string_view> v =
      absl::StrSplit(it->second.string(), absl::MaxSplits('/', 3));
  url_full_path_ = absl::StrCat("/", v[3]);
  it = options.credential_source.object().find("headers");
  if (it != options.credential_source.object().end()) {
    if (it->second.type() != Json::Type::kObject) {
      *error = GRPC_ERROR_CREATE(
          "The JSON value of credential source headers is not an object.");
      return;
    }
    for (auto const& header : it->second.object()) {
      headers_[header.first] = header.second.string();
    }
  }
  it = options.credential_source.object().find("format");
  if (it != options.credential_source.object().end()) {
    const Json& format_json = it->second;
    if (format_json.type() != Json::Type::kObject) {
      *error = GRPC_ERROR_CREATE(
          "The JSON value of credential source format is not an object.");
      return;
    }
    auto format_it = format_json.object().find("type");
    if (format_it == format_json.object().end()) {
      *error = GRPC_ERROR_CREATE("format.type field not present.");
      return;
    }
    if (format_it->second.type() != Json::Type::kString) {
      *error = GRPC_ERROR_CREATE("format.type field must be a string.");
      return;
    }
    format_type_ = format_it->second.string();
    if (format_type_ == "json") {
      format_it = format_json.object().find("subject_token_field_name");
      if (format_it == format_json.object().end()) {
        *error = GRPC_ERROR_CREATE(
            "format.subject_token_field_name field must be present if the "
            "format is in Json.");
        return;
      }
      if (format_it->second.type() != Json::Type::kString) {
        *error = GRPC_ERROR_CREATE(
            "format.subject_token_field_name field must be a string.");
        return;
      }
      format_subject_token_field_name_ = format_it->second.string();
    }
  }
}

std::string UrlExternalAccountCredentials::debug_string() {
  return absl::StrCat("UrlExternalAccountCredentials{Audience:", audience(),
                      ")");
}

UniqueTypeName UrlExternalAccountCredentials::Type() {
  static UniqueTypeName::Factory kFactory("UrlExternalAccountCredentials");
  return kFactory.Create();
}

OrphanablePtr<ExternalAccountCredentials::FetchBody>
UrlExternalAccountCredentials::RetrieveSubjectToken(
    Timestamp deadline,
    absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done) {
  auto url_for_request =
      URI::Create(url_.scheme(), url_.authority(), url_full_path_,
                  {} /* query params */, "" /* fragment */);
  if (!url_for_request.ok()) {
    return MakeOrphanable<NoOpFetchBody>(
        event_engine(), std::move(on_done),
        absl_status_to_grpc_error(url_for_request.status()));
  }
  return MakeOrphanable<HttpFetchBody>(
      [&](grpc_http_response* response, grpc_closure* on_http_response) {
        grpc_http_request request;
        memset(&request, 0, sizeof(grpc_http_request));
        request.path = gpr_strdup(url_full_path_.c_str());
        grpc_http_header* headers = nullptr;
        request.hdr_count = headers_.size();
        headers = static_cast<grpc_http_header*>(
            gpr_malloc(sizeof(grpc_http_header) * request.hdr_count));
        int i = 0;
        for (auto const& header : headers_) {
          headers[i].key = gpr_strdup(header.first.c_str());
          headers[i].value = gpr_strdup(header.second.c_str());
          ++i;
        }
        request.hdrs = headers;
        RefCountedPtr<grpc_channel_credentials> http_request_creds;
        if (url_.scheme() == "http") {
          http_request_creds = RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create());
        } else {
          http_request_creds = CreateHttpRequestSSLCredentials();
        }
        auto http_request =
            HttpRequest::Get(std::move(*url_for_request), /*args=*/nullptr,
                             pollent(), &request, deadline, on_http_response,
                             response, std::move(http_request_creds));
        http_request->Start();
        grpc_http_request_destroy(&request);
        return http_request;
      },
      [this, on_done = std::move(on_done)](
          absl::StatusOr<std::string> response_body) mutable {
        if (!response_body.ok()) {
          on_done(std::move(response_body));
          return;
        }
        if (format_type_ == "json") {
          auto response_json = JsonParse(*response_body);
          if (!response_json.ok() ||
              response_json->type() != Json::Type::kObject) {
            on_done(GRPC_ERROR_CREATE(
                "The format of response is not a valid json object."));
            return;
          }
          auto response_it =
              response_json->object().find(format_subject_token_field_name_);
          if (response_it == response_json->object().end()) {
            on_done(GRPC_ERROR_CREATE("Subject token field not present."));
            return;
          }
          if (response_it->second.type() != Json::Type::kString) {
            on_done(GRPC_ERROR_CREATE("Subject token field must be a string."));
            return;
          }
          on_done(response_it->second.string());
          return;
        }
        on_done(std::move(response_body));
      });
}

absl::string_view UrlExternalAccountCredentials::CredentialSourceType() {
  return "url";
}

}  // namespace grpc_core
