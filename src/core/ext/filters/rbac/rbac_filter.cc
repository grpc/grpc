//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/rbac/rbac_filter.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/status/status.h"

#include <grpc/grpc_security.h>

#include "src/core/ext/filters/rbac/rbac_service_config_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/security/authorization/authorization_engine.h"
#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/core/lib/transport/transport_impl.h"

namespace grpc_core {

ArenaPromise<ServerMetadataHandle> RbacFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  // Fetch and apply the rbac policy from the service config.
  auto* service_config_call_data = static_cast<ServiceConfigCallData*>(
      GetContext<
          grpc_call_context_element>()[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA]
          .value);
  auto* method_params = static_cast<RbacMethodParsedConfig*>(
      service_config_call_data->GetMethodParsedConfig(
          service_config_parser_index_));
  if (method_params == nullptr) {
    return Immediate(ServerMetadataFromStatus(
        absl::PermissionDeniedError("No RBAC policy found.")));
  } else {
    auto* authorization_engine = method_params->authorization_engine(index_);
    if (authorization_engine
            ->Evaluate(EvaluateArgs(call_args.client_initial_metadata.get(),
                                    &per_channel_evaluate_args_))
            .type == AuthorizationEngine::Decision::Type::kDeny) {
      return Immediate(ServerMetadataFromStatus(
          absl::PermissionDeniedError("Unauthorized RPC rejected")));
    }
  }
  return next_promise_factory(std::move(call_args));
}

const grpc_channel_filter RbacFilter::kFilterVtable =
    MakePromiseBasedFilter<RbacFilter, FilterEndpoint::kServer>("rbac_filter");

RbacFilter::RbacFilter(size_t index,
                       EvaluateArgs::PerChannelArgs per_channel_evaluate_args)
    : index_(index),
      service_config_parser_index_(RbacServiceConfigParser::ParserIndex()),
      per_channel_evaluate_args_(std::move(per_channel_evaluate_args)) {}

absl::StatusOr<RbacFilter> RbacFilter::Create(const ChannelArgs& args,
                                              ChannelFilter::Args filter_args) {
  auto* auth_context = args.GetObject<grpc_auth_context>();
  if (auth_context == nullptr) {
    return GRPC_ERROR_CREATE("No auth context found");
  }
  auto* transport = args.GetObject<grpc_transport>();
  if (transport == nullptr) {
    // This should never happen since the transport is always set on the server
    // side.
    return GRPC_ERROR_CREATE("No transport configured");
  }
  return RbacFilter(grpc_channel_stack_filter_instance_number(
                        filter_args.channel_stack(),
                        filter_args.uninitialized_channel_element()),
                    EvaluateArgs::PerChannelArgs(
                        auth_context, grpc_transport_get_endpoint(transport)));
}

void RbacFilterRegister(CoreConfiguration::Builder* builder) {
  RbacServiceConfigParser::Register(builder);
}

}  // namespace grpc_core
