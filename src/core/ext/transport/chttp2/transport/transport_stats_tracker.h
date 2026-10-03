//
//
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
//
//

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_TRANSPORT_STATS_TRACKER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_TRANSPORT_STATS_TRACKER_H

#include <grpc/credentials.h>

#include <memory>

#include "src/core/ext/transport/chttp2/transport/http2_stats_collector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/transport/auth_context.h"

namespace grpc_core {
namespace http2 {

// Encapsulates telemetry and metrics collection for HTTP/2 transports.
// Shared between Http2ClientTransport and Http2ServerTransport.
class TransportStatsTracker {
 public:
  // Based on CHTTP2's grpc_chttp2_transport constructor in chttp2_transport.cc.
  explicit TransportStatsTracker(const ChannelArgs& channel_args)
      : stats_collector_(CreateHttp2StatsCollector(
            channel_args.GetObject<grpc_auth_context>())) {}
  ~TransportStatsTracker() = default;

  // TransportStatsTracker is non-copyable and non-movable.
  TransportStatsTracker(const TransportStatsTracker&) = delete;
  TransportStatsTracker& operator=(const TransportStatsTracker&) = delete;
  TransportStatsTracker(TransportStatsTracker&&) = delete;
  TransportStatsTracker& operator=(TransportStatsTracker&&) = delete;

  // Access the underlying stats collector for HPACK table initialization.
  std::shared_ptr<Http2StatsCollector> stats_collector_shared() const {
    return stats_collector_;
  }

 private:
  std::shared_ptr<Http2StatsCollector> stats_collector_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_TRANSPORT_STATS_TRACKER_H
