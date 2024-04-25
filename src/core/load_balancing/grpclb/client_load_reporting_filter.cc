//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/load_balancing/grpclb/client_load_reporting_filter.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/types/optional.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/load_balancing/grpclb/grpclb_client_stats.h"

namespace grpc_core {

const NoInterceptor ClientLoadReportingFilter::Call::OnServerToClientMessage;
const NoInterceptor ClientLoadReportingFilter::Call::OnClientToServerMessage;
const NoInterceptor ClientLoadReportingFilter::Call::OnFinalize;

const grpc_channel_filter ClientLoadReportingFilter::kFilter =
    MakePromiseBasedFilter<ClientLoadReportingFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata>(
        "client_load_reporting");

absl::StatusOr<std::unique_ptr<ClientLoadReportingFilter>>
ClientLoadReportingFilter::Create(const ChannelArgs&, ChannelFilter::Args) {
  return std::make_unique<ClientLoadReportingFilter>();
}

void ClientLoadReportingFilter::Call::OnClientInitialMetadata(
    ClientMetadata& client_initial_metadata) {
  // Handle client initial metadata.
  // Grab client stats object from metadata.
  auto client_stats_md =
      client_initial_metadata.Take(GrpcLbClientStatsMetadata());
  if (client_stats_md.has_value()) {
    client_stats_.reset(*client_stats_md);
  }
}

void ClientLoadReportingFilter::Call::OnServerInitialMetadata(ServerMetadata&) {
  saw_initial_metadata_ = true;
}

void ClientLoadReportingFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& server_trailing_metadata) {
  if (client_stats_ != nullptr) {
    client_stats_->AddCallFinished(
        server_trailing_metadata.get(GrpcStreamNetworkState()) ==
            GrpcStreamNetworkState::kNotSentOnWire,
        saw_initial_metadata_);
  }
}

}  // namespace grpc_core
