// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_SERVER_AUTHZ_FILTER_H
#define GRPC_SRC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_SERVER_AUTHZ_FILTER_H

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"

#include <grpc/grpc_security.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/security/authorization/authorization_policy_provider.h"
#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

class GrpcServerAuthzFilter final
    : public ImplementChannelFilter<GrpcServerAuthzFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<GrpcServerAuthzFilter> Create(const ChannelArgs& args,
                                                      ChannelFilter::Args);

  class Call {
   public:
    absl::Status OnClientInitialMetadata(ClientMetadata& md,
                                         GrpcServerAuthzFilter* filter);
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;
  };

 private:
  GrpcServerAuthzFilter(
      RefCountedPtr<grpc_auth_context> auth_context, grpc_endpoint* endpoint,
      RefCountedPtr<grpc_authorization_policy_provider> provider);

  bool IsAuthorized(ClientMetadata& initial_metadata);

  RefCountedPtr<grpc_auth_context> auth_context_;
  EvaluateArgs::PerChannelArgs per_channel_evaluate_args_;
  RefCountedPtr<grpc_authorization_policy_provider> provider_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_SERVER_AUTHZ_FILTER_H
