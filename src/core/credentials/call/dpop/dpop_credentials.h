// Copyright 2025 gRPC authors.
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

// DPoP (RFC 9449) call credentials. EXPERIMENTAL.
// Binds the access token to a proof-of-possession key AND to the current TLS
// connection's channel binding (tls-unique / tls-exporter), so a stolen token
// (and even a stolen PoP key) cannot be replayed on a different TLS session.

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_DPOP_DPOP_CREDENTIALS_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_DPOP_DPOP_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/credentials/call/call_credentials.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#define GRPC_DPOP_PROOF_METADATA_KEY "dpop"
#define GRPC_DPOP_AUTHORIZATION_SCHEME "DPoP "

namespace grpc_core {

// DPoP sender-constrained call credentials (RFC 9449, EXPERIMENTAL).
// access_token   : raw OAuth2 token (no scheme prefix)
// ec_private_pem : PEM-encoded EC private key (P-256 / ES256)
class grpc_dpop_credentials final : public grpc_call_credentials {
 public:
  grpc_dpop_credentials(absl::string_view access_token,
                        absl::string_view ec_private_pem);
  ~grpc_dpop_credentials() override;

  void Orphaned() override {}

  ArenaPromise<absl::StatusOr<ClientMetadataHandle>> GetRequestMetadata(
      ClientMetadataHandle initial_metadata,
      const GetRequestMetadataArgs* args) override;

  std::string debug_string() override {
    return "DpopCredentials{token:present}";
  }

  static UniqueTypeName Type();
  UniqueTypeName type() const override { return Type(); }

 private:
  int cmp_impl(const grpc_call_credentials* other) const override {
    return QsortCompare(static_cast<const grpc_call_credentials*>(this), other);
  }

  // Build a DPoP proof JWT covering htu and the TLS channel binding.
  std::string BuildDpopProof(absl::string_view htu,
                             absl::string_view tls_channel_binding) const;

  Slice authorization_value_;
  std::string ec_private_pem_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_DPOP_DPOP_CREDENTIALS_H
