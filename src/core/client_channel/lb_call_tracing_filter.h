// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_LB_CALL_TRACING_FILTER_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_LB_CALL_TRACING_FILTER_H

#include <memory>

#include "absl/status/statusor.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

// A filter to handle updating with the call tracer and LB subchannel
// call tracker inside the LB call.
class LbCallTracingFilter final
    : public ImplementChannelFilter<LbCallTracingFilter> {
 public:
  static const grpc_channel_filter kFilter;

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& metadata);
    static const NoInterceptor OnClientToServerMessage;
    void OnClientToServerHalfClose();

    void OnServerInitialMetadata(ServerMetadata& metadata);
    static const NoInterceptor OnServerToClientMessage;
    void OnServerTrailingMetadata(ServerMetadata& metadata);

    void OnFinalize(const grpc_call_final_info*);

   private:
    Slice peer_string_;
  };

  static absl::StatusOr<std::unique_ptr<LbCallTracingFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<LbCallTracingFilter>();
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_LB_CALL_TRACING_FILTER_H
