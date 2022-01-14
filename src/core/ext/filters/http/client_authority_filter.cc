/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/client_authority_filter.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_stack_type.h"

namespace grpc_core {
namespace {

class ClientAuthorityChannelFilter {
 public:
  static constexpr bool is_client() { return true; }
  static constexpr const char* name() { return "authority"; }

  static absl::StatusOr<ClientAuthorityChannelFilter> Create(
      const grpc_channel_args* args) {
    const grpc_arg* default_authority_arg =
        grpc_channel_args_find(args, GRPC_ARG_DEFAULT_AUTHORITY);
    if (default_authority_arg == nullptr) {
      return absl::InvalidArgumentError(
          "GRPC_ARG_DEFAULT_AUTHORITY channel arg. not found. Note that direct "
          "channels must explicitly specify a value for this argument.");
    }
    const char* default_authority_str =
        grpc_channel_arg_get_string(default_authority_arg);
    if (default_authority_str == nullptr) {
      return absl::InvalidArgumentError(
          "GRPC_ARG_DEFAULT_AUTHORITY channel arg. must be a string");
    }
    return absl::StatusOr<ClientAuthorityChannelFilter>(
        ClientAuthorityChannelFilter(
            Slice::FromCopiedString(default_authority_str)));
  }

  // Construct a promise for once call.
  ArenaPromise<TrailingMetadata> MakeCallPromise(
      InitialMetadata initial_metadata,
      NextPromiseFactory next_promise_factory) {
    // If no authority is set, set the default authority.
    if (initial_metadata->get_pointer(HttpAuthorityMetadata()) == nullptr) {
      initial_metadata->Set(HttpAuthorityMetadata(), default_authority_.Ref());
    }
    // We have no asynchronous work, so we can just ask the next promise to run,
    // passing down initial_metadata.
    return next_promise_factory(std::move(initial_metadata));
  }

 private:
  explicit ClientAuthorityChannelFilter(Slice default_authority)
      : default_authority_(std::move(default_authority)) {}
  Slice default_authority_;
};

}  // namespace
}  // namespace grpc_core

const grpc_channel_filter grpc_client_authority_filter =
    grpc_core::MakePromiseBasedFilter<
        grpc_core::ClientAuthorityChannelFilter>();

static bool add_client_authority_filter(grpc_channel_stack_builder* builder) {
  const grpc_channel_args* channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  const grpc_arg* disable_client_authority_filter_arg = grpc_channel_args_find(
      channel_args, GRPC_ARG_DISABLE_CLIENT_AUTHORITY_FILTER);
  if (disable_client_authority_filter_arg != nullptr) {
    const bool is_client_authority_filter_disabled =
        grpc_channel_arg_get_bool(disable_client_authority_filter_arg, false);
    if (is_client_authority_filter_disabled) {
      return true;
    }
  }
  return grpc_channel_stack_builder_prepend_filter(
      builder, &grpc_client_authority_filter, nullptr, nullptr);
}

namespace grpc_core {
void RegisterClientAuthorityFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(GRPC_CLIENT_SUBCHANNEL, INT_MAX,
                                         add_client_authority_filter);
  builder->channel_init()->RegisterStage(GRPC_CLIENT_DIRECT_CHANNEL, INT_MAX,
                                         add_client_authority_filter);
}
}  // namespace grpc_core
