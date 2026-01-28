//
// Copyright 2024 gRPC authors.
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

#include "src/core/credentials/call/gcp_service_account_identity/gcp_service_account_identity_credentials.h"

#include "src/core/call/metadata.h"
#include "src/core/credentials/call/jwt_util.h"
#include "src/core/credentials/call/token_fetcher/token_fetcher_credentials.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/uri.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// JwtTokenFetcherCallCredentials
//

OrphanablePtr<TokenFetcherCredentials::FetchRequest>
JwtTokenFetcherCallCredentials::FetchToken(
    Timestamp deadline,
    absl::AnyInvocable<
        void(absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>>)>
        on_done) {
  return MakeOrphanable<HttpTokenFetcherCredentials::HttpFetchRequest>(
      this, deadline,
      [on_done = std::move(on_done)](
          absl::StatusOr<grpc_http_response> response) mutable {
        if (!response.ok()) {
          on_done(response.status());
          return;
        }
        absl::string_view body(response->body, response->body_length);
        auto expiration_time = GetJwtExpirationTime(body);
        if (!expiration_time.ok()) {
          on_done(expiration_time.status());
          return;
        }
        on_done(MakeRefCounted<Token>(
            Slice::FromCopiedString(absl::StrCat("Bearer ", body)),
            *expiration_time));
      });
}

//
// GcpServiceAccountIdentityCallCredentials
//

std::string GcpServiceAccountIdentityCallCredentials::debug_string() {
  return absl::StrCat("GcpServiceAccountIdentityCallCredentials(", audience_,
                      ")");
}

UniqueTypeName GcpServiceAccountIdentityCallCredentials::Type() {
  static UniqueTypeName::Factory kFactory("GcpServiceAccountIdentity");
  return kFactory.Create();
}

OrphanablePtr<HttpRequest>
GcpServiceAccountIdentityCallCredentials::StartHttpRequest(
    grpc_polling_entity* pollent, Timestamp deadline,
    grpc_http_response* response, grpc_closure* on_complete) {
  grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                             const_cast<char*>("Google")};
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  request.hdr_count = 1;
  request.hdrs = &header;
  // TODO(ctiller): Carry the memory quota in ctx and share it with the host
  // channel. This would allow us to cancel an authentication query when under
  // extreme memory pressure.
  auto uri = URI::Create(
      "http", /*user_info=*/"", "metadata.google.internal.",
      "/computeMetadata/v1/instance/service-accounts/default/identity",
      {{"audience", audience_}}, /*fragment=*/"");
  CHECK_OK(uri);  // params are hardcoded
  auto http_request =
      HttpRequest::Get(std::move(*uri), /*args=*/nullptr, pollent, &request,
                       deadline, on_complete, response,
                       RefCountedPtr<grpc_channel_credentials>(
                           grpc_insecure_credentials_create()));
  http_request->Start();
  return http_request;
}

}  // namespace grpc_core
