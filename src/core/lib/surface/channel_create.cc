//
// Copyright 2015 gRPC authors.
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

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/log.h>

#include "src/core/client_channel/client_channel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/legacy_channel.h"

namespace grpc_core {

absl::StatusOr<OrphanablePtr<Channel>> Channel::Create(
    std::string target, ChannelArgs args,
    grpc_channel_stack_type channel_stack_type, Transport* optional_transport) {
  // TODO(roth): When we finish migrating to the v3 stack, we can remove
  // this check, since the server code should no longer use this code-path.
  if (grpc_channel_stack_type_is_client(channel_stack_type)) {
    global_stats().IncrementClientChannelsCreated();
    // Set default authority if needed.
    if (!args.GetString(GRPC_ARG_DEFAULT_AUTHORITY).has_value()) {
      auto ssl_override = args.GetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
      if (ssl_override.has_value()) {
        args = args.Set(GRPC_ARG_DEFAULT_AUTHORITY,
                        std::string(ssl_override.value()));
      }
    }
    // Check whether channelz is enabled.
    if (args.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
            .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT)) {
      // Get parameters needed to create the channelz node.
      const size_t channel_tracer_max_memory = std::max(
          0,
          args.GetInt(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE)
              .value_or(GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT));
      const bool is_internal_channel =
          args.GetBool(GRPC_ARG_CHANNELZ_IS_INTERNAL_CHANNEL).value_or(false);
      // Create the channelz node.
      std::string channelz_node_target{target.empty() ? "unknown" : target};
      auto channelz_node = MakeRefCounted<channelz::ChannelNode>(
          channelz_node_target, channel_tracer_max_memory, is_internal_channel);
      channelz_node->AddTraceEvent(
          channelz::ChannelTrace::Severity::Info,
          grpc_slice_from_static_string("Channel created"));
      // Add channelz node to channel args.
      // We remove the is_internal_channel arg, since we no longer need it.
      args = args.Remove(GRPC_ARG_CHANNELZ_IS_INTERNAL_CHANNEL)
                 .SetObject(std::move(channelz_node));
    }
  } else {  // server channel
    global_stats().IncrementServerChannelsCreated();
  }
  // Set compression options.
  grpc_compression_options compression_options;
  grpc_compression_options_init(&compression_options);
  auto default_level = args.GetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL);
  if (default_level.has_value()) {
    compression_options.default_level.is_set = true;
    compression_options.default_level.level = Clamp(
        static_cast<grpc_compression_level>(*default_level),
        GRPC_COMPRESS_LEVEL_NONE,
        static_cast<grpc_compression_level>(GRPC_COMPRESS_LEVEL_COUNT - 1));
  }
  auto default_algorithm =
      args.GetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM);
  if (default_algorithm.has_value()) {
    compression_options.default_algorithm.is_set = true;
    compression_options.default_algorithm.algorithm =
        Clamp(static_cast<grpc_compression_algorithm>(*default_algorithm),
              GRPC_COMPRESS_NONE,
              static_cast<grpc_compression_algorithm>(
                  GRPC_COMPRESS_ALGORITHMS_COUNT - 1));
  }
  auto enabled_algorithms_bitset =
      args.GetInt(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET);
  if (enabled_algorithms_bitset.has_value()) {
    compression_options.enabled_algorithms_bitset =
        *enabled_algorithms_bitset | 1 /* always support no compression */;
  }
  // Add transport to args.
  if (optional_transport != nullptr) {
    args = args.SetObject(optional_transport);
  }
  // Delegate to appropriate channel impl.
  if (!IsCallV3Enabled()) {
    return LegacyChannel::Create(std::move(target), std::move(args),
                                 channel_stack_type, compression_options);
  }
  return ClientChannel::Create(std::move(target), std::move(args),
                               channel_stack_type, compression_options);
}

}  // namespace grpc_core
