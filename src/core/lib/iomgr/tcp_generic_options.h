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

#ifndef GRPC_CORE_LIB_IOMGR_TCP_GENERIC_OPTIONS_H
#define GRPC_CORE_LIB_IOMGR_TCP_GENERIC_OPTIONS_H

#include <grpc/support/port_platform.h>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/lib/resource_quota/api.h"

struct TcpGenericOptions {
  int tcp_read_chunk_size;
  int tcp_min_read_chunk_size;
  int tcp_max_read_chunk_size;
  int tcp_tx_zerocopy_send_bytes_threshold;
  int tcp_tx_zerocopy_max_simultaneous_sends;
  int tcp_tx_zero_copy_enabled;
  int keep_alive_time_ms;
  int keep_alive_timeout_ms;
  int expand_wildcard_addrs;
  int allow_reuse_port;
  grpc_core::RefCountedPtr<grpc_core::ResourceQuota> resource_quota;
  struct grpc_socket_mutator* socket_mutator;
  TcpGenericOptions() : resource_quota(nullptr), socket_mutator(nullptr) {}
  // Move ctor
  TcpGenericOptions(TcpGenericOptions&& other) noexcept {
    socket_mutator = absl::exchange(other.socket_mutator, nullptr);
    resource_quota = std::move(other.resource_quota);
    ResetAllIntegerOptions(other);
  }
  // Move assignment
  TcpGenericOptions& operator=(TcpGenericOptions&& other) noexcept {
    if (socket_mutator != nullptr) {
      grpc_socket_mutator_unref(socket_mutator);
    }
    socket_mutator = absl::exchange(other.socket_mutator, nullptr);
    resource_quota = std::move(other.resource_quota);
    ResetAllIntegerOptions(other);
    return *this;
  }
  // Copy ctor
  TcpGenericOptions(const TcpGenericOptions& other) {
    if (other.socket_mutator != nullptr) {
      socket_mutator = grpc_socket_mutator_ref(other.socket_mutator);
    } else {
      socket_mutator = nullptr;
    }
    resource_quota = other.resource_quota;
    ResetAllIntegerOptions(other);
  }
  // Copy assignment
  TcpGenericOptions& operator=(const TcpGenericOptions& other) {
    if (&other == this) {
      return *this;
    }
    if (socket_mutator != nullptr) {
      grpc_socket_mutator_unref(socket_mutator);
      socket_mutator = nullptr;
    }
    if (other.socket_mutator != nullptr) {
      socket_mutator = grpc_socket_mutator_ref(other.socket_mutator);
    }
    resource_quota = other.resource_quota;
    ResetAllIntegerOptions(other);
    return *this;
  }
  // Destructor.
  ~TcpGenericOptions() {
    if (socket_mutator != nullptr) {
      grpc_socket_mutator_unref(socket_mutator);
    }
  }

 private:
  void ResetAllIntegerOptions(const TcpGenericOptions& other) {
    tcp_read_chunk_size = other.tcp_read_chunk_size;
    tcp_min_read_chunk_size = other.tcp_min_read_chunk_size;
    tcp_max_read_chunk_size = other.tcp_max_read_chunk_size;
    tcp_tx_zerocopy_send_bytes_threshold =
        other.tcp_tx_zerocopy_send_bytes_threshold;
    tcp_tx_zerocopy_max_simultaneous_sends =
        other.tcp_tx_zerocopy_max_simultaneous_sends;
    tcp_tx_zero_copy_enabled = other.tcp_tx_zero_copy_enabled;
    keep_alive_time_ms = other.keep_alive_time_ms;
    keep_alive_timeout_ms = other.keep_alive_timeout_ms;
    expand_wildcard_addrs = other.expand_wildcard_addrs;
    allow_reuse_port = other.allow_reuse_port;
  }
};

// Initialize the tcp generic options struct members. This method can be used
// to initialize dynamically allocated struct objects.
void grpc_tcp_generic_options_init(TcpGenericOptions* options);

// Unref members of the tcp generic options struct.
void grpc_tcp_generic_options_destroy(TcpGenericOptions* options);

// Given an EndpointConfig object, returns a TcpGenericOptions struct
// constructed from it
TcpGenericOptions TcpOptionsFromEndpointConfig(
    const grpc_event_engine::experimental::EndpointConfig& config);

// Given an grpc_core::ChannelArgs object, returns a TcpGenericOptions
// struct constructed from it
TcpGenericOptions TcpOptionsFromChannelArgs(const grpc_core::ChannelArgs& args);

// Given an grpc_channel_args pointer, returns a TcpGenericOptions
// struct constructed from it
TcpGenericOptions TcpOptionsFromChannelArgs(const grpc_channel_args* args);

/* For use in tests only */
grpc_core::ChannelArgs TcpOptionsIntoChannelArgs(
    const TcpGenericOptions& options);

#endif  // GRPC_CORE_LIB_IOMGR_TCP_GENERIC_OPTIONS_H
