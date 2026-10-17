// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_VIRTUAL_CLIENT_CALL_TRACER_FILTER_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_VIRTUAL_CLIENT_CALL_TRACER_FILTER_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class VirtualClientCallTracerFilter
    : public ImplementChannelFilter<VirtualClientCallTracerFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::string_view TypeName() { return "virtual_client_call_tracer"; }

  static absl::StatusOr<std::unique_ptr<VirtualClientCallTracerFilter>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/);

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& client_initial_metadata);
    void OnServerInitialMetadata(ServerMetadata& server_initial_metadata);
    void OnServerTrailingMetadata(ServerMetadata& server_trailing_metadata);
    void OnFinalize(const grpc_call_final_info* final_info);

    void OnClientToServerMessage(const grpc_core::Message& msg);
    void OnServerToClientMessage(const grpc_core::Message& msg);

    static inline const NoInterceptor OnClientToServerHalfClose;

    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }

   private:
    std::atomic<uint64_t> outgoing_bytes_{0};
    std::atomic<uint64_t> incoming_bytes_{0};
  };
};

void RegisterVirtualClientCallTracerFilter(CoreConfiguration::Builder* builder);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_VIRTUAL_CLIENT_CALL_TRACER_FILTER_H
