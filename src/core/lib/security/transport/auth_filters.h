/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_TRANSPORT_AUTH_FILTERS_H
#define GRPC_CORE_LIB_SECURITY_TRANSPORT_AUTH_FILTERS_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/transport/transport.h"

extern const grpc_channel_filter grpc_client_auth_filter;
extern const grpc_channel_filter grpc_server_auth_filter;

namespace grpc_core {

// Handles calling out to credentials to fill in metadata per call.
// Use private inheritance to AuthMetadataContext since we don't want to vend
// this type generally, and we don't want to increase the size of this type if
// we can help it.
class ClientAuthFilter final : private AuthMetadataContext {
 public:
  static absl::StatusOr<ClientAuthFilter> Create(const grpc_channel_args* args);

  // Construct a promise for one call.
  ArenaPromise<TrailingMetadata> MakeCallPromise(
      ClientInitialMetadata initial_metadata,
      NextPromiseFactory next_promise_factory);

 private:
  struct PartialAuthContext {
    absl::string_view host_and_port;
    absl::string_view method_name;
    absl::string_view url_scheme;
    absl::string_view service;
    std::string ServiceUrl() const;
  };

  ClientAuthFilter(
      RefCountedPtr<grpc_channel_security_connector> security_connector,
      RefCountedPtr<grpc_auth_context> auth_context);

  ArenaPromise<absl::StatusOr<ClientInitialMetadata>> GetCallCredsMetadata(
      ClientInitialMetadata initial_metadata);

  PartialAuthContext GetPartialAuthContext(
      const ClientInitialMetadata& initial_metadata) const;

  std::string JwtServiceUrl(
      const ClientInitialMetadata& metadata) const override;
  grpc_auth_metadata_context MakeLegacyContext(
      const ClientInitialMetadata& metadata) const override;

  RefCountedPtr<grpc_channel_security_connector> security_connector_;
  RefCountedPtr<grpc_auth_context> auth_context_;
};

}  // namespace grpc_core

// Exposed for testing purposes only.
// Check if the channel's security level is higher or equal to
// that of call credentials to make a decision whether the transfer
// of call credentials should be allowed or not.
bool grpc_check_security_level(grpc_security_level channel_level,
                               grpc_security_level call_cred_level);

#endif /* GRPC_CORE_LIB_SECURITY_TRANSPORT_AUTH_FILTERS_H */
