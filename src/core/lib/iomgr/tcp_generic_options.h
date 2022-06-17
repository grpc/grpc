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

typedef struct grpc_tcp_generic_options {
  absl::flat_hash_map<absl::string_view, int> int_options;
  grpc_core::RefCountedPtr<grpc_core::ResourceQuota> resource_quota = nullptr;
  struct grpc_socket_mutator* socket_mutator = nullptr;
  grpc_tcp_generic_options()
      : resource_quota(nullptr), socket_mutator(nullptr) {}
  grpc_tcp_generic_options(const struct grpc_tcp_generic_options& other) {
    if (other.socket_mutator != nullptr) {
      socket_mutator = grpc_socket_mutator_ref(other.socket_mutator);
    }
    resource_quota = other.resource_quota;
  }
  grpc_tcp_generic_options& operator=(
      const struct grpc_tcp_generic_options& other) {
    if (other.socket_mutator != nullptr) {
      socket_mutator = grpc_socket_mutator_ref(other.socket_mutator);
    }
    resource_quota = other.resource_quota;
    return *this;
  }
  grpc_tcp_generic_options(struct grpc_tcp_generic_options&& other) {
    socket_mutator = other.socket_mutator;
    other.socket_mutator = nullptr;
    resource_quota = std::move(other.resource_quota);
  }
  ~grpc_tcp_generic_options() {
    if (socket_mutator != nullptr) {
      grpc_socket_mutator_unref(socket_mutator);
    }
  }
} grpc_tcp_generic_options;

// Given an EndpointConfig object, returns a grpc_tcp_generic_options struct
// constructed from it
grpc_tcp_generic_options TcpOptionsFromEndpointConfig(
    const grpc_event_engine::experimental::EndpointConfig& config);

// Given an grpc_core::ChannelArgs object, returns a grpc_tcp_generic_options
// struct constructed from it
grpc_tcp_generic_options TcpOptionsFromChannelArgs(
    const grpc_core::ChannelArgs& args);

// Given an grpc_channel_args pointer, returns a grpc_tcp_generic_options
// struct constructed from it
grpc_tcp_generic_options TcpOptionsFromChannelArgs(
    const grpc_channel_args* args);

/* For use in tests only */
grpc_core::ChannelArgs TcpOptionsIntoChannelArgs(
    const grpc_tcp_generic_options& options);

#endif  //  GRPC_CORE_LIB_IOMGR_TCP_GENERIC_OPTIONS_H
