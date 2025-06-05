//
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
//

#include "src/core/credentials/call/dual/dual_call_credentials.h"

#include <grpc/grpc_security.h>

#include "absl/status/statusor.h"
#include "src/core/credentials/transport/alts/alts_security_connector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

DualCallCredentials::DualCallCredentials(
    RefCountedPtr<grpc_call_credentials> tls_credentials,
    RefCountedPtr<grpc_call_credentials> alts_credentials)
    : tls_credentials_(std::move(tls_credentials)),
      alts_credentials_(std::move(alts_credentials)) {}

DualCallCredentials::~DualCallCredentials() = default;

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
DualCallCredentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const GetRequestMetadataArgs* args) {
  bool use_alts = false;
  if (args != nullptr) {
    auto auth_context = args->auth_context;
    if (auth_context != nullptr &&
        grpc_auth_context_peer_is_authenticated(auth_context.get()) == 1) {
      // This channel is authenticated.
      grpc_auth_property_iterator property_it =
          grpc_auth_context_find_properties_by_name(
              auth_context.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
      const grpc_auth_property* property =
          grpc_auth_property_iterator_next(&property_it);
      use_alts =
          property != nullptr &&
          strcmp(property->value, GRPC_ALTS_TRANSPORT_SECURITY_TYPE) == 0;
    }
  }
  return (use_alts ? alts_credentials_ : tls_credentials_)
      ->GetRequestMetadata(std::move(initial_metadata), args);
}

void DualCallCredentials::Orphaned() {}

grpc_core::UniqueTypeName DualCallCredentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Dual");
  return kFactory.Create();
}

std::string DualCallCredentials::debug_string() {
  return "DualCallCredentials";
}

}  // namespace grpc_core
