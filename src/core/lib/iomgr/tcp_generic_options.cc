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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/tcp_generic_options.h"

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"

using ::grpc_event_engine::experimental::EndpointConfig;

#define MAX_CHUNK_SIZE (32 * 1024 * 1024)

namespace {

constexpr int kDefaultReadChunkSize = 8192;
constexpr int kDefaultMinReadChunksize = 256;
constexpr int kDefaultMaxReadChunksize = 4 * 1024 * 1024;
constexpr int kZerocpTxEnabledDefault = 0;
constexpr int kMaxChunkSize = 32 * 1024 * 1024;
constexpr int kDefaultMaxSends = 4;
constexpr size_t kDefaultSendBytesThreshold = 16 * 1024;  // 16KB

int Clamp(int default_value, int min_value, int max_value, int actual_value) {
  if (actual_value < min_value || actual_value > max_value) {
    return default_value;
  }
  return actual_value;
}

int GetConfigValue(const EndpointConfig& config, absl::string_view key,
                   int min_value, int max_value, int default_value) {
  EndpointConfig::Setting value = config.Get(key);
  if (absl::holds_alternative<int>(value)) {
    return Clamp(default_value, min_value, max_value, absl::get<int>(value));
  }
  return default_value;
}
}  // namespace

void grpc_tcp_generic_options_init(TcpGenericOptions* options) {
  if (options != nullptr) {
    new (options) TcpGenericOptions();
  }
}

void grpc_tcp_generic_options_destroy(TcpGenericOptions* options) {
  if (options != nullptr) {
    if (options->socket_mutator != nullptr) {
      grpc_socket_mutator_unref(options->socket_mutator);
      options->socket_mutator = nullptr;
    }
    options->resource_quota.reset(nullptr);
  }
}

TcpGenericOptions TcpOptionsFromEndpointConfig(const EndpointConfig& config) {
  EndpointConfig::Setting value;
  TcpGenericOptions options;
  grpc_tcp_generic_options_init(&options);
  options.tcp_read_chunk_size =
      GetConfigValue(config, GRPC_ARG_TCP_READ_CHUNK_SIZE, 1, kMaxChunkSize,
                     kDefaultReadChunkSize);
  options.tcp_min_read_chunk_size =
      GetConfigValue(config, GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE, 1, kMaxChunkSize,
                     kDefaultMinReadChunksize);
  options.tcp_max_read_chunk_size =
      GetConfigValue(config, GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE, 1, kMaxChunkSize,
                     kDefaultMaxReadChunksize);
  options.tcp_tx_zerocopy_send_bytes_threshold =
      GetConfigValue(config, GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD, 0,
                     INT_MAX, kDefaultSendBytesThreshold);
  options.tcp_tx_zerocopy_max_simultaneous_sends =
      GetConfigValue(config, GRPC_ARG_TCP_TX_ZEROCOPY_MAX_SIMULT_SENDS, 0,
                     INT_MAX, kDefaultMaxSends);
  options.tcp_tx_zero_copy_enabled = GetConfigValue(
      config, GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED, 0, 1, kZerocpTxEnabledDefault);
  options.keep_alive_time_ms =
      GetConfigValue(config, GRPC_ARG_KEEPALIVE_TIME_MS, 1, INT_MAX, 0);
  options.keep_alive_timeout_ms =
      GetConfigValue(config, GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 1, INT_MAX, 0);
  options.expand_wildcard_addrs =
      GetConfigValue(config, GRPC_ARG_EXPAND_WILDCARD_ADDRS, 1, INT_MAX, 0);
  options.allow_reuse_port =
      GetConfigValue(config, GRPC_ARG_ALLOW_REUSEPORT, 1, INT_MAX, 0);

  if (options.tcp_min_read_chunk_size > options.tcp_max_read_chunk_size) {
    options.tcp_min_read_chunk_size = options.tcp_max_read_chunk_size;
  }
  options.tcp_read_chunk_size = grpc_core::Clamp(
      options.tcp_read_chunk_size, options.tcp_min_read_chunk_size,
      options.tcp_max_read_chunk_size);

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

TcpGenericOptions TcpOptionsFromChannelArgs(
    const grpc_core::ChannelArgs& args) {
  auto config = grpc_event_engine::experimental::CreateEndpointConfig(args);
  return TcpOptionsFromEndpointConfig(*config);
}

TcpGenericOptions TcpOptionsFromChannelArgs(const grpc_channel_args* args) {
  if (args == nullptr) {
    return TcpGenericOptions();
  }
  return TcpOptionsFromChannelArgs(grpc_core::ChannelArgs::FromC(args));
}

grpc_core::ChannelArgs TcpOptionsIntoChannelArgs(
    const TcpGenericOptions& options) {
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
  args = args.SetIfUnset(GRPC_ARG_TCP_READ_CHUNK_SIZE,
                         options.tcp_read_chunk_size);
  args = args.SetIfUnset(GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE,
                         options.tcp_read_chunk_size);
  args = args.SetIfUnset(GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE,
                         options.tcp_read_chunk_size);
  args =
      args.SetIfUnset(GRPC_ARG_KEEPALIVE_TIME_MS, options.keep_alive_time_ms);
  args = args.SetIfUnset(GRPC_ARG_KEEPALIVE_TIMEOUT_MS,
                         options.keep_alive_timeout_ms);
  args = args.SetIfUnset(GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD,
                         options.tcp_tx_zerocopy_send_bytes_threshold);
  args = args.SetIfUnset(GRPC_ARG_TCP_TX_ZEROCOPY_MAX_SIMULT_SENDS,
                         options.tcp_tx_zerocopy_max_simultaneous_sends);
  args = args.SetIfUnset(GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED,
                         options.tcp_tx_zero_copy_enabled);
  args = args.SetIfUnset(GRPC_ARG_EXPAND_WILDCARD_ADDRS,
                         options.expand_wildcard_addrs);
  return args.SetIfUnset(GRPC_ARG_ALLOW_REUSEPORT, options.allow_reuse_port);
}
