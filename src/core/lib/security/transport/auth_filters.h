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

#include "absl/status/statusor.h"

#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/transport/transport.h"

extern const grpc_channel_filter grpc_server_auth_filter;

namespace grpc_core {

// Handles calling out to credentials to fill in metadata per call.
class ClientAuthFilter final : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ClientAuthFilter> Create(const ChannelArgs& args,
                                                 ChannelFilter::Args);

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  ClientAuthFilter(
      RefCountedPtr<grpc_channel_security_connector> security_connector,
      RefCountedPtr<grpc_auth_context> auth_context);

  ArenaPromise<absl::StatusOr<CallArgs>> GetCallCredsMetadata(
      CallArgs call_args);

  // Contains refs to security connector and auth context.
  grpc_call_credentials::GetRequestMetadataArgs args_;
};

}  // namespace grpc_core

// Exposed for testing purposes only.
// Check if the channel's security level is higher or equal to
// that of call credentials to make a decision whether the transfer
// of call credentials should be allowed or not.
bool grpc_check_security_level(grpc_security_level channel_level,
                               grpc_security_level call_cred_level);

#endif /* GRPC_CORE_LIB_SECURITY_TRANSPORT_AUTH_FILTERS_H */
