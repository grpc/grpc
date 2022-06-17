// Copyright 2022 The gRPC Authors
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

#include "src/core/lib/iomgr/tcp_generic_options.h"

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"

using ::grpc_event_engine::experimental::EndpointConfig;

namespace {
void CopyIntegerConfigValueToTCPOptions(const EndpointConfig& config,
                                        grpc_tcp_generic_options& options,
                                        absl::string_view key) {
  EndpointConfig::Setting value = config.Get(key);
  if (absl::holds_alternative<int>(value)) {
    options.int_options.insert_or_assign(key, absl::get<int>(value));
  }
}

void CopyIntegerConfigValueToChannelArgs(
    const grpc_tcp_generic_options& options, grpc_core::ChannelArgs& args,
    absl::string_view key) {
  auto it = options.int_options.find(key);
  if (it != options.int_options.end()) {
    args = args.SetIfUnset(key, it->second);
  }
}
}  // namespace

grpc_tcp_generic_options TcpOptionsFromEndpointConfig(
    const EndpointConfig& config) {
  EndpointConfig::Setting value;
  grpc_tcp_generic_options options;
  CopyIntegerConfigValueToTCPOptions(config, options,
                                     GRPC_ARG_TCP_READ_CHUNK_SIZE);
  CopyIntegerConfigValueToTCPOptions(config, options,
                                     GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE);
  CopyIntegerConfigValueToTCPOptions(config, options,
                                     GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE);
  CopyIntegerConfigValueToTCPOptions(config, options,
                                     GRPC_ARG_KEEPALIVE_TIME_MS);
  CopyIntegerConfigValueToTCPOptions(config, options,
                                     GRPC_ARG_KEEPALIVE_TIMEOUT_MS);
  CopyIntegerConfigValueToTCPOptions(
      config, options, GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD);
  CopyIntegerConfigValueToTCPOptions(config, options,
                                     GRPC_ARG_TCP_TX_ZEROCOPY_MAX_SIMULT_SENDS);
  CopyIntegerConfigValueToTCPOptions(config, options,
                                     GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED);
  CopyIntegerConfigValueToTCPOptions(config, options,
                                     GRPC_ARG_EXPAND_WILDCARD_ADDRS);
  CopyIntegerConfigValueToTCPOptions(config, options, GRPC_ARG_ALLOW_REUSEPORT);

  value = config.Get(GRPC_ARG_RESOURCE_QUOTA);
  if (absl::holds_alternative<void*>(value)) {
    options.resource_quota =
        reinterpret_cast<grpc_core::ResourceQuota*>(absl::get<void*>(value))
            ->Ref();
  }
  value = config.Get(GRPC_ARG_SOCKET_MUTATOR);
  if (absl::holds_alternative<void*>(value)) {
    options.socket_mutator = grpc_socket_mutator_ref(
        static_cast<grpc_socket_mutator*>(absl::get<void*>(value)));
  }
  return options;
}

grpc_tcp_generic_options TcpOptionsFromChannelArgs(
    const grpc_core::ChannelArgs& args) {
  return TcpOptionsFromEndpointConfig(
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args));
}

grpc_tcp_generic_options TcpOptionsFromChannelArgs(
    const grpc_channel_args* args) {
  if (args == nullptr) {
    return grpc_tcp_generic_options();
  }
  return TcpOptionsFromChannelArgs(grpc_core::ChannelArgs::FromC(args));
}

grpc_core::ChannelArgs TcpOptionsIntoChannelArgs(
    const grpc_tcp_generic_options& options) {
  grpc_core::ChannelArgs args;
  if (options.socket_mutator != nullptr) {
    static const grpc_arg_pointer_vtable socket_mutator_vtable = {
        // copy
        [](void* p) -> void* {
          return grpc_socket_mutator_ref(static_cast<grpc_socket_mutator*>(p));
        },
        // destroy
        [](void* p) {
          grpc_socket_mutator_unref(static_cast<grpc_socket_mutator*>(p));
        },
        // compare
        [](void* a, void* b) {
          return grpc_socket_mutator_compare(
              static_cast<grpc_socket_mutator*>(a),
              static_cast<grpc_socket_mutator*>(b));
        }};
    args = args.Set(GRPC_ARG_SOCKET_MUTATOR,
                    grpc_core::ChannelArgs::Pointer(
                        grpc_socket_mutator_ref(options.socket_mutator),
                        &socket_mutator_vtable));
  }
  if (options.resource_quota != nullptr) {
    args = args.Set(GRPC_ARG_RESOURCE_QUOTA, options.resource_quota);
  }
  CopyIntegerConfigValueToChannelArgs(options, args,
                                      GRPC_ARG_TCP_READ_CHUNK_SIZE);
  CopyIntegerConfigValueToChannelArgs(options, args,
                                      GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE);
  CopyIntegerConfigValueToChannelArgs(options, args,
                                      GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE);
  CopyIntegerConfigValueToChannelArgs(options, args,
                                      GRPC_ARG_KEEPALIVE_TIME_MS);
  CopyIntegerConfigValueToChannelArgs(options, args,
                                      GRPC_ARG_KEEPALIVE_TIMEOUT_MS);
  CopyIntegerConfigValueToChannelArgs(
      options, args, GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD);
  CopyIntegerConfigValueToChannelArgs(
      options, args, GRPC_ARG_TCP_TX_ZEROCOPY_MAX_SIMULT_SENDS);
  CopyIntegerConfigValueToChannelArgs(options, args,
                                      GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED);
  CopyIntegerConfigValueToChannelArgs(options, args,
                                      GRPC_ARG_EXPAND_WILDCARD_ADDRS);
  CopyIntegerConfigValueToChannelArgs(options, args, GRPC_ARG_ALLOW_REUSEPORT);
  return args;
}
