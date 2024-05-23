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

#include "src/core/lib/event_engine/extensions/tcp_trace.h"

#include "absl/strings/string_view.h"

#include <grpc/support/metrics.h>

#include "src/core/lib/channel/metrics.h"
#include "src/core/lib/channel/tcp_tracer.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

const auto kTCPConnectionMetricsMinRtt =
    grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
        "grpc.tcp.min_rtt",
        "EXPERIMENTAL. Records TCP's current estimate of minimum round trip "
        "time (RTT), typically used as an indication of the network health "
        "between two endpoints.",
        "{s}", /*enable_by_default=*/true)
        .OptionalLabels(grpc_core::kMetricLabelPeerAddress,
                        grpc_core::kMetricLabelLocalAddress)
        .Build();

const auto kTCPConnectionMetricsDeliveryRate =
    grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
        "grpc.tcp.delivery_rate",
        "EXPERIMENTAL. Records latest throughput measured of the TCP "
        "connection.",
        "{bit/s}", /*enable_by_default=*/true)
        .OptionalLabels(grpc_core::kMetricLabelPeerAddress,
                        grpc_core::kMetricLabelLocalAddress)
        .Build();

const auto kTCPConnectionMetricsPacketSend =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.tcp.packets_sent",
        "EXPERIMENTAL. Records total packets TCP sends in the calculation "
        "period.",
        "{packet}", /*enable_by_default=*/true)
        .OptionalLabels(grpc_core::kMetricLabelPeerAddress,
                        grpc_core::kMetricLabelLocalAddress)
        .Build();

const auto kTCPConnectionMetricsPacketRetx =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.tcp.packets_retransmitted",
        "EXPERIMENTAL. Records total packets lost in the calculation period, "
        "including lost or spuriously retransmitted packets.",
        "{packet}", /*enable_by_default=*/true)
        .OptionalLabels(grpc_core::kMetricLabelPeerAddress,
                        grpc_core::kMetricLabelLocalAddress)
        .Build();

const auto kTCPConnectionMetricsPacketSpuriousRetx =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.tcp.packets_spurious_retransmitted",
        "EXPERIMENTAL. Records total packets spuriously retransmitted packets "
        "in the calculation period. These are retransmissions that TCP later "
        "discovered unnecessary.",
        "{packet}", /*enable_by_default=*/true)
        .OptionalLabels(grpc_core::kMetricLabelPeerAddress,
                        grpc_core::kMetricLabelLocalAddress)
        .Build();

void Http2TransportTcpTracer::RecordConnectionMetrics(
    ConnectionMetrics metrics) {
  // This will be called periodically by Fathom.
  // For cumulative stats, compute and get deltas to stats plugins.
  if (metrics.packet_sent.has_value() && metrics.packet_retx.has_value() &&
      metrics.packet_spurious_retx.has_value()) {
    grpc_core::MutexLock lock(&mu_);
    int packet_sent = metrics.packet_sent.value() -
                      connection_metrics_.packet_sent.value_or(0);
    int packet_retx = metrics.packet_retx.value() -
                      connection_metrics_.packet_retx.value_or(0);
    int packet_spurious_retx =
        metrics.packet_spurious_retx.value() -
        connection_metrics_.packet_spurious_retx.value_or(0);
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .AddCounter(kTCPConnectionMetricsPacketSend, packet_sent, {},
                    {grpc_core::kMetricLabelPeerAddress,
                     grpc_core::kMetricLabelLocalAddress});
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .AddCounter(kTCPConnectionMetricsPacketRetx, packet_retx, {},
                    {grpc_core::kMetricLabelPeerAddress,
                     grpc_core::kMetricLabelLocalAddress});
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .AddCounter(kTCPConnectionMetricsPacketSpuriousRetx,
                    packet_spurious_retx, {},
                    {grpc_core::kMetricLabelPeerAddress,
                     grpc_core::kMetricLabelLocalAddress});
    connection_metrics_.packet_sent = metrics.packet_sent;
    connection_metrics_.packet_retx = metrics.packet_retx;
    connection_metrics_.packet_spurious_retx = metrics.packet_spurious_retx;
  }
  // For non-cumulative stats: Report to stats plugins.
  if (metrics.min_rtt.has_value()) {
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .RecordHistogram(
            kTCPConnectionMetricsMinRtt,
            static_cast<float>(metrics.min_rtt.value()) / 1000000.0, {},
            {grpc_core::kMetricLabelPeerAddress,
             grpc_core::kMetricLabelLocalAddress});
  }
  if (metrics.delivery_rate.has_value()) {
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .RecordHistogram(kTCPConnectionMetricsDeliveryRate,
                         metrics.delivery_rate.value(), {},
                         {grpc_core::kMetricLabelPeerAddress,
                          grpc_core::kMetricLabelLocalAddress});
  }
}
}  // namespace experimental
}  // namespace grpc_event_engine