/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <string>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include <grpc/support/time.h>

#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/jwt/json_token.h"

class grpc_service_account_jwt_access_credentials
    : public grpc_call_credentials {
 public:
  grpc_service_account_jwt_access_credentials(grpc_auth_json_key key,
                                              gpr_timespec token_lifetime);
  ~grpc_service_account_jwt_access_credentials() override;

  bool get_request_metadata(grpc_polling_entity* pollent,
                            grpc_auth_metadata_context context,
                            grpc_core::CredentialsMetadataArray* md_array,
                            grpc_closure* on_request_metadata,
                            grpc_error_handle* error) override;

  void cancel_get_request_metadata(
      grpc_core::CredentialsMetadataArray* md_array,
      grpc_error_handle error) override;

  const gpr_timespec& jwt_lifetime() const { return jwt_lifetime_; }
  const grpc_auth_json_key& key() const { return key_; }

  std::string debug_string() override {
    return absl::StrFormat(
        "JWTAccessCredentials{ExpirationTime:%s}",
        absl::FormatTime(absl::FromUnixMicros(
            static_cast<int64_t>(gpr_timespec_to_micros(jwt_lifetime_)))));
  };

 private:
  // Have a simple cache for now with just 1 entry. We could have a map based on
  // the service_url for a more sophisticated one.
  gpr_mu cache_mu_;
  struct Cache {
    grpc_core::Slice jwt_value;
    std::string service_url;
    gpr_timespec jwt_expiration;
  };
  absl::optional<Cache> cached_;

  grpc_auth_json_key key_;
  gpr_timespec jwt_lifetime_;
};

// Private constructor for jwt credentials from an already parsed json key.
// Takes ownership of the key.
grpc_core::RefCountedPtr<grpc_call_credentials>
grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
    grpc_auth_json_key key, gpr_timespec token_lifetime);

namespace grpc_core {

// Exposed for testing purposes only.
absl::StatusOr<std::string> RemoveServiceNameFromJwtUri(absl::string_view uri);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_CREDENTIALS_H */
