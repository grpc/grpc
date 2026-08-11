//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_GDCH_SERVICE_ACCOUNT_GDCH_SERVICE_ACCOUNT_CREDENTIALS_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_GDCH_SERVICE_ACCOUNT_GDCH_SERVICE_ACCOUNT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/credentials/call/token_fetcher/token_fetcher_credentials.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/uri.h"
#include "src/core/util/validation_errors.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// GDCH Service Account credentials.
//
// Uses the service account to create a JWT assertion which is then exchanged
// for a STS bearer token.
//
// JSON Schema for the service account key file:
// {
//   "type": "object",
//   "properties": {
//     "type": { "type": "string", "const": "gdch_service_account" },
//     "format_version": { "type": "string", "const": "1" },
//     "project": { "type": "string" },
//     "private_key_id": { "type": "string" },
//     "private_key": { "type": "string" },
//     "name": { "type": "string" },
//     "ca_cert_path": { "type": "string" },
//     "token_uri": { "type": "string" }
//   },
//   "required": [
//     "type",
//     "format_version",
//     "project",
//     "private_key_id",
//     "private_key",
//     "name",
//     "token_uri"
//   ]
// }
class GDCHServiceAccountCredentials final : public HttpTokenFetcherCredentials {
 public:
  struct Info {
    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<Info>()
              .Field("type", &Info::type)
              .Field("format_version", &Info::format_version)
              .Field("project", &Info::project_id)
              .Field("private_key_id", &Info::private_key_id)
              .Field("private_key", &Info::private_key)
              .Field("name", &Info::service_identity_name)
              .OptionalField("ca_cert_path", &Info::ca_cert_path)
              .Field("token_uri", &Info::token_uri)
              .Finish();
      return loader;
    }

    void JsonPostLoad(const Json& source, const JsonArgs& args,
                      ValidationErrors* errors);

    std::string type;
    std::string format_version;
    std::string project_id;
    std::string private_key_id;
    std::string private_key;
    std::string service_identity_name;
    std::optional<std::string> ca_cert_path;
    std::string token_uri;
  };

  static absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> Create(
      const Json& key_file_contents, std::string audience);

  GDCHServiceAccountCredentials(Info info, std::string audience, URI token_url);

  const std::optional<std::string>& ca_cert_path() const {
    return info_.ca_cert_path;
  }

  std::string debug_string() override;

  UniqueTypeName type() const override;

  OrphanablePtr<HttpRequest> StartHttpRequest(
      grpc_polling_entity* pollent, Timestamp deadline,
      grpc_http_response* response, grpc_closure* on_complete) override;

  absl::StatusOr<RefCountedPtr<Token>> ExtractToken(
      const grpc_http_response& response) override;

 private:
  friend class GDCHServiceAccountCredentialsTest;
  friend grpc_call_credentials* ::grpc_gdch_service_account_credentials_create(
      const char* json_string, const char* audience_string);

  // OpenSSL outputs DER format signatures by default. RFC-7515 (JWT/JWS)
  // specifies the Raw format should be used.
  enum class SignatureFormat { kDER, kRaw };

  struct AssertionComponents {
    std::string header;
    std::string claim;
  };

  struct GrpcDeleter {
    void operator()(grpc_http_request* ptr);
  };
  using GrpcHttpRequestUniquePtr =
      std::unique_ptr<grpc_http_request, GrpcDeleter>;

  // Signs a string with the private key from a PEM container.
  //
  // @return the signature as an *unencoded* byte array.
  static absl::StatusOr<std::vector<std::uint8_t>> SignUsingSha256(
      const std::string& str, const std::string& pem_contents,
      SignatureFormat format);

  static AssertionComponents AssertionComponentsFromInfo(
      const Info& info, std::chrono::system_clock::time_point now);

  static absl::StatusOr<std::string> MakeJWTAssertion(
      const std::string& header, const std::string& payload,
      const std::string& pem_contents, SignatureFormat format);

  static absl::StatusOr<std::string> CreateRequestBody(
      const Info& info, const std::string& audience);

  static absl::StatusOr<GrpcHttpRequestUniquePtr> FormatHttpRequest(
      const Info& info, const std::string& audience, const URI& token_url);

  static absl::StatusOr<std::string> ParseHttpResponse(
      absl::string_view response_body);

  Info info_;
  std::string audience_;
  URI token_url_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_GDCH_SERVICE_ACCOUNT_GDCH_SERVICE_ACCOUNT_CREDENTIALS_H
