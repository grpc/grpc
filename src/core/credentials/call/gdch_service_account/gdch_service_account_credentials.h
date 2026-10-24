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

#include <memory>
#include <optional>
#include <string>

#include "src/core/credentials/call/token_fetcher/token_fetcher_credentials.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/uri.h"
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
  static absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> Create(
      absl::string_view key_file_contents, std::string audience);

  GDCHServiceAccountCredentials(std::string private_key_id,
                                std::string private_key,
                                std::string service_account_identity,
                                std::optional<std::string> ca_cert_path,
                                URI token_url, std::string audience);

  const std::optional<std::string>& ca_cert_path() const {
    return ca_cert_path_;
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
  // @return the signature as an *unencoded* string.
  static absl::StatusOr<std::string> SignUsingSha256(
      absl::string_view str, absl::string_view pem_contents,
      SignatureFormat format);

  AssertionComponents CreateAssertionComponents(Timestamp now) const;

  static absl::StatusOr<std::string> MakeJWTAssertion(
      absl::string_view header, absl::string_view claim,
      absl::string_view pem_contents, SignatureFormat format);

  absl::StatusOr<std::string> CreateRequestBody() const;

  absl::StatusOr<GrpcHttpRequestUniquePtr> FormatHttpRequest() const;

  static absl::StatusOr<std::string> ParseHttpResponse(
      absl::string_view response_body);

  std::string private_key_id_;
  std::string private_key_;
  std::string service_account_identity_;
  std::optional<std::string> ca_cert_path_;
  URI token_url_;
  std::string audience_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_GDCH_SERVICE_ACCOUNT_GDCH_SERVICE_ACCOUNT_CREDENTIALS_H
