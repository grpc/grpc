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

#include <grpc/support/port_platform.h>

#include "src/core/load_balancing/grpclb/client_load_reporting_filter.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/types/optional.h"

#include "src/core/load_balancing/grpclb/grpclb_client_stats.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
const grpc_channel_filter ClientLoadReportingFilter::kFilter =
    MakePromiseBasedFilter<ClientLoadReportingFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata>(
        "client_load_reporting");

absl::StatusOr<ClientLoadReportingFilter> ClientLoadReportingFilter::Create(
    const ChannelArgs&, ChannelFilter::Args) {
  return ClientLoadReportingFilter();
}

ArenaPromise<ServerMetadataHandle> ClientLoadReportingFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  // Stats object to update.
  RefCountedPtr<GrpcLbClientStats> client_stats;

  // Handle client initial metadata.
  // Grab client stats object from metadata.
  auto client_stats_md =
      call_args.client_initial_metadata->Take(GrpcLbClientStatsMetadata());
  if (client_stats_md.has_value()) {
    client_stats.reset(*client_stats_md);
  }

  auto* saw_initial_metadata = GetContext<Arena>()->New<bool>(false);
  call_args.server_initial_metadata->InterceptAndMap(
      [saw_initial_metadata](ServerMetadataHandle md) {
        *saw_initial_metadata = true;
        return md;
      });

  return Map(next_promise_factory(std::move(call_args)),
             [saw_initial_metadata, client_stats = std::move(client_stats)](
                 ServerMetadataHandle trailing_metadata) {
               if (client_stats != nullptr) {
                 client_stats->AddCallFinished(
                     trailing_metadata->get(GrpcStreamNetworkState()) ==
                         GrpcStreamNetworkState::kNotSentOnWire,
                     *saw_initial_metadata);
               }
               return trailing_metadata;
             });
}
}  // namespace grpc_core
