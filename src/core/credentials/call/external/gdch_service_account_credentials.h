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

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_EXTERNAL_GDCH_SERVICE_ACCOUNT_CREDENTIALS_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_EXTERNAL_GDCH_SERVICE_ACCOUNT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "src/core/credentials/call/external/external_account_credentials.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/uri.h"
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
class GDCHServiceAccountCredentials final : public ExternalAccountCredentials {
 public:
  // OpenSSL outputs DER format signatures by default. RFC-7515 (JWT/JWS)
  // specifies the Raw format should be used.
  enum class SignatureFormat { kDER, kRaw };

  // Signs a string with the private key from a PEM container.
  //
  // @return the signature as an *unencoded* byte array.
  static absl::StatusOr<std::vector<std::uint8_t>> SignUsingSha256(
      std::string const& str, std::string const& pem_contents,
      SignatureFormat format);

  struct Info {
    std::string type;
    std::string format_version;
    std::string project_id;
    std::string private_key_id;
    std::string private_key;
    std::string service_identity_name;
    std::optional<std::string> ca_cert_path = std::nullopt;
    std::string token_uri;
  };

  static absl::StatusOr<Info> ParseServiceAccountJson(Json const& json);

  static absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> Create(
      Json const& key_file_contents, std::string audience,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine = nullptr);

  GDCHServiceAccountCredentials(
      Info info, std::string audience,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine);

  static std::pair<std::string, std::string> AssertionComponentsFromInfo(
      Info const& info, std::chrono::system_clock::time_point now);

  static absl::StatusOr<std::string> MakeJWTAssertion(
      std::string const& header, std::string const& payload,
      std::string const& pem_contents, SignatureFormat format);

  static absl::StatusOr<std::string> CreateRequestBody(
      Info const& info, std::string const& audience);

  struct GrpcDeleter {
    void operator()(grpc_http_request* ptr);
  };
  using GrpcHttpRequestUniquePtr =
      std::unique_ptr<grpc_http_request, GrpcDeleter>;

  static absl::StatusOr<GrpcHttpRequestUniquePtr> FormatHttpRequest(
      Info const& info, std::string const& audience);

  static absl::StatusOr<std::string> ParseHttpResponse(
      std::string const& response_body);

  static UniqueTypeName Type();

  std::optional<std::string> ca_cert_path() const { return info_.ca_cert_path; }

  std::string debug_string() override;

  UniqueTypeName type() const override { return Type(); }

 private:
  friend class GDCHServiceAccountCredentialsTest;

  OrphanablePtr<FetchRequest> FetchToken(
      Timestamp deadline,
      absl::AnyInvocable<void(absl::StatusOr<RefCountedPtr<Token>>)> on_done)
      override;

  OrphanablePtr<FetchBody> RetrieveSubjectToken(
      Timestamp deadline,
      absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done) override;

  absl::string_view CredentialSourceType() override;

  class GDCHFetchRequest;

  Info info_;
  std::string audience_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_EXTERNAL_GDCH_SERVICE_ACCOUNT_CREDENTIALS_H
