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

#include "src/core/ext/filters/rbac/rbac_filter.h"

#include <grpc/grpc_security.h>

#include <memory>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/authorization/authorization_engine.h"
#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/latent_see.h"
#include "absl/status/status.h"

namespace grpc_core {

absl::Status RbacFilter::Call::OnClientInitialMetadata(ClientMetadata& md,
                                                       RbacFilter* filter) {
  GRPC_LATENT_SEE_SCOPE("RbacFilter::Call::OnClientInitialMetadata");
  auto decision = filter->authorization_engine_.Evaluate(
      EvaluateArgs(&md, &filter->per_channel_evaluate_args_));
  if (decision.type == AuthorizationEngine::Decision::Type::kDeny) {
    return absl::PermissionDeniedError("Unauthorized RPC rejected");
  }
  return absl::OkStatus();
}

const grpc_channel_filter RbacFilter::kFilterVtable =
    MakePromiseBasedFilter<RbacFilter, FilterEndpoint::kServer>();

absl::StatusOr<std::unique_ptr<RbacFilter>> RbacFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  auto* auth_context = args.GetObject<grpc_auth_context>();
  if (auth_context == nullptr) {
    return GRPC_ERROR_CREATE("No auth context found");
  }
  auto* transport = args.GetObject<Transport>();
  if (transport == nullptr) {
    // This should never happen since the transport is always set on the server
    // side.
    return GRPC_ERROR_CREATE("No transport configured");
  }
  if (filter_args.config() == nullptr) {
    return absl::InternalError("no config passed to RBAC filter");
  }
  if (filter_args.config()->type() != Config::Type()) {
    return absl::InternalError(
        absl::StrCat("wrong config type passed to RBAC filter: ",
                     filter_args.config()->type().name()));
  }
  return std::make_unique<RbacFilter>(
      DownCast<const Config&>(*filter_args.config()).rbac,
      EvaluateArgs::PerChannelArgs(auth_context, args));
}

RbacFilter::RbacFilter(const Rbac& rbac,
                       EvaluateArgs::PerChannelArgs per_channel_evaluate_args)
    : authorization_engine_(rbac),
      per_channel_evaluate_args_(std::move(per_channel_evaluate_args)) {}

}  // namespace grpc_core
