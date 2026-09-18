// Copyright 2025 The gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/posix_write_event_sink.h"

namespace grpc_event_engine::experimental {

std::optional<size_t> PosixWriteEventSink::GetMetricKey(
    absl::string_view name) {
  if (name == "delivery_rate") {
    return static_cast<size_t>(Metric::kDeliveryRate);
  }
  if (name == "is_delivery_rate_app_limited") {
    return static_cast<size_t>(Metric::kIsDeliveryRateAppLimited);
  }
  if (name == "packet_retx") return static_cast<size_t>(Metric::kPacketRetx);
  if (name == "packet_spurious_retx") {
    return static_cast<size_t>(Metric::kPacketSpuriousRetx);
  }
  if (name == "packet_sent") return static_cast<size_t>(Metric::kPacketSent);
  if (name == "packet_delivered") {
    return static_cast<size_t>(Metric::kPacketDelivered);
  }
  if (name == "packet_delivered_ce") {
    return static_cast<size_t>(Metric::kPacketDeliveredCE);
  }
  if (name == "data_retx") return static_cast<size_t>(Metric::kDataRetx);
  if (name == "data_sent") return static_cast<size_t>(Metric::kDataSent);
  if (name == "data_notsent") {
    return static_cast<size_t>(Metric::kDataNotSent);
  }
  if (name == "pacing_rate") return static_cast<size_t>(Metric::kPacingRate);
  if (name == "min_rtt") return static_cast<size_t>(Metric::kMinRtt);
  if (name == "srtt") return static_cast<size_t>(Metric::kSrtt);
  if (name == "congestion_window") {
    return static_cast<size_t>(Metric::kCongestionWindow);
  }
  if (name == "snd_ssthresh") {
    return static_cast<size_t>(Metric::kSndSsthresh);
  }
  if (name == "reordering") return static_cast<size_t>(Metric::kReordering);
  if (name == "recurring_retrans") {
    return static_cast<size_t>(Metric::kRecurringRetrans);
  }
  if (name == "busy_usec") return static_cast<size_t>(Metric::kBusyUsec);
  if (name == "rwnd_limited_usec") {
    return static_cast<size_t>(Metric::kRwndLimitedUsec);
  }
  if (name == "sndbuf_limited_usec") {
    return static_cast<size_t>(Metric::kSndbufLimitedUsec);
  }
  return std::nullopt;
}

std::optional<absl::string_view> PosixWriteEventSink::GetMetricName(
    size_t key) {
  switch (key) {
    case static_cast<size_t>(Metric::kDeliveryRate):
      return "delivery_rate";
    case static_cast<size_t>(Metric::kIsDeliveryRateAppLimited):
      return "is_delivery_rate_app_limited";
    case static_cast<size_t>(Metric::kPacketRetx):
      return "packet_retx";
    case static_cast<size_t>(Metric::kPacketSpuriousRetx):
      return "packet_spurious_retx";
    case static_cast<size_t>(Metric::kPacketSent):
      return "packet_sent";
    case static_cast<size_t>(Metric::kPacketDelivered):
      return "packet_delivered";
    case static_cast<size_t>(Metric::kPacketDeliveredCE):
      return "packet_delivered_ce";
    case static_cast<size_t>(Metric::kDataRetx):
      return "data_retx";
    case static_cast<size_t>(Metric::kDataSent):
      return "data_sent";
    case static_cast<size_t>(Metric::kDataNotSent):
      return "data_notsent";
    case static_cast<size_t>(Metric::kPacingRate):
      return "pacing_rate";
    case static_cast<size_t>(Metric::kMinRtt):
      return "min_rtt";
    case static_cast<size_t>(Metric::kSrtt):
      return "srtt";
    case static_cast<size_t>(Metric::kCongestionWindow):
      return "congestion_window";
    case static_cast<size_t>(Metric::kSndSsthresh):
      return "snd_ssthresh";
    case static_cast<size_t>(Metric::kReordering):
      return "reordering";
    case static_cast<size_t>(Metric::kRecurringRetrans):
      return "recurring_retrans";
    case static_cast<size_t>(Metric::kBusyUsec):
      return "busy_usec";
    case static_cast<size_t>(Metric::kRwndLimitedUsec):
      return "rwnd_limited_usec";
    case static_cast<size_t>(Metric::kSndbufLimitedUsec):
      return "sndbuf_limited_usec";
    default:
      return std::nullopt;
  }
}

void PosixWriteEventSink::RecordEvent(EventEngine::Endpoint::WriteEvent event,
                                      absl::Time timestamp,
                                      const ConnectionMetrics& conn_metrics) {
  if (!requested_events_.test(static_cast<int>(event))) return;
  std::vector<EventEngine::Endpoint::WriteMetric> metrics;
  auto maybe_add = [this, &metrics](Metric metric, auto value) {
    if (requested_metrics_ == nullptr ||
        !requested_metrics_->IsSet(static_cast<int>(metric)) ||
        !value.has_value()) {
      return;
    }
    metrics.push_back(EventEngine::Endpoint::WriteMetric{
        static_cast<size_t>(metric),
        static_cast<int64_t>(value.value()),
    });
  };
  maybe_add(Metric::kDeliveryRate, conn_metrics.delivery_rate);
  maybe_add(Metric::kIsDeliveryRateAppLimited,
            conn_metrics.is_delivery_rate_app_limited);
  maybe_add(Metric::kPacketRetx, conn_metrics.packet_retx);
  maybe_add(Metric::kPacketSpuriousRetx, conn_metrics.packet_spurious_retx);
  maybe_add(Metric::kPacketSent, conn_metrics.packet_sent);
  maybe_add(Metric::kPacketDelivered, conn_metrics.packet_delivered);
  maybe_add(Metric::kPacketDeliveredCE, conn_metrics.packet_delivered_ce);
  maybe_add(Metric::kDataRetx, conn_metrics.data_retx);
  maybe_add(Metric::kDataSent, conn_metrics.data_sent);
  maybe_add(Metric::kDataNotSent, conn_metrics.data_notsent);
  maybe_add(Metric::kPacingRate, conn_metrics.pacing_rate);
  maybe_add(Metric::kMinRtt, conn_metrics.min_rtt);
  maybe_add(Metric::kSrtt, conn_metrics.srtt);
  maybe_add(Metric::kCongestionWindow, conn_metrics.congestion_window);
  maybe_add(Metric::kSndSsthresh, conn_metrics.snd_ssthresh);
  maybe_add(Metric::kReordering, conn_metrics.reordering);
  maybe_add(Metric::kRecurringRetrans, conn_metrics.recurring_retrans);
  maybe_add(Metric::kBusyUsec, conn_metrics.busy_usec);
  maybe_add(Metric::kRwndLimitedUsec, conn_metrics.rwnd_limited_usec);
  maybe_add(Metric::kSndbufLimitedUsec, conn_metrics.sndbuf_limited_usec);
  on_event_(event, timestamp, std::move(metrics));
}

}  // namespace grpc_event_engine::experimental
