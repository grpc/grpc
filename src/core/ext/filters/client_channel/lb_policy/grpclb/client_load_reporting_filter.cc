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

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/types/optional.h"

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
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

  auto* server_initial_metadata = call_args.server_initial_metadata;

  return Seq(next_promise_factory(std::move(call_args)),
             [server_initial_metadata, client_stats = std::move(client_stats)](
                 ServerMetadataHandle trailing_metadata) {
               if (client_stats != nullptr) {
                 client_stats->AddCallFinished(
                     trailing_metadata->get(GrpcStreamNetworkState()) ==
                         GrpcStreamNetworkState::kNotSentOnWire,
                     NowOrNever(server_initial_metadata->Wait()).has_value());
               }
               return trailing_metadata;
             });
}
}  // namespace grpc_core
