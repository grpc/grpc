//
//
// Copyright 2018 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/client_authority_filter.h"

#include <limits.h>

#include <functional>
#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

absl::StatusOr<ClientAuthorityFilter> ClientAuthorityFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  absl::optional<absl::string_view> default_authority =
      args.GetString(GRPC_ARG_DEFAULT_AUTHORITY);
  if (!default_authority.has_value()) {
    return absl::InvalidArgumentError(
        "GRPC_ARG_DEFAULT_AUTHORITY string channel arg. not found. Note that "
        "direct channels must explicitly specify a value for this argument.");
  }
  return ClientAuthorityFilter(Slice::FromCopiedString(*default_authority));
}

ArenaPromise<ServerMetadataHandle> ClientAuthorityFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  // If no authority is set, set the default authority.
  if (call_args.client_initial_metadata->get_pointer(HttpAuthorityMetadata()) ==
      nullptr) {
    call_args.client_initial_metadata->Set(HttpAuthorityMetadata(),
                                           default_authority_.Ref());
  }
  // We have no asynchronous work, so we can just ask the next promise to run,
  // passing down initial_metadata.
  return next_promise_factory(std::move(call_args));
}

const grpc_channel_filter ClientAuthorityFilter::kFilter =
    MakePromiseBasedFilter<ClientAuthorityFilter, FilterEndpoint::kClient>(
        "authority");

namespace {
bool add_client_authority_filter(ChannelStackBuilder* builder) {
  if (builder->channel_args()
          .GetBool(GRPC_ARG_DISABLE_CLIENT_AUTHORITY_FILTER)
          .value_or(false)) {
    return true;
  }
  builder->PrependFilter(&ClientAuthorityFilter::kFilter);
  return true;
}
}  // namespace

void RegisterClientAuthorityFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(GRPC_CLIENT_SUBCHANNEL, INT_MAX,
                                         add_client_authority_filter);
  builder->channel_init()->RegisterStage(GRPC_CLIENT_DIRECT_CHANNEL, INT_MAX,
                                         add_client_authority_filter);
}

}  // namespace grpc_core
