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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_WRITE_EVENT_SINK_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_WRITE_EVENT_SINK_H

#include <grpc/event_engine/event_engine.h>

#include "absl/base/no_destructor.h"
#include "absl/functional/any_invocable.h"
#include "src/core/util/bitset.h"

namespace grpc_event_engine::experimental {

class PosixWriteEventSink {
 public:
  struct ConnectionMetrics {  // Delivery rate in Bytes/s.
    std::optional<uint64_t> delivery_rate;
    // If the delivery rate is limited by the application, this is set to true.
    std::optional<bool> is_delivery_rate_app_limited;
    // Total packets retransmitted.
    std::optional<uint32_t> packet_retx;
    // Total packets retransmitted spuriously. This metric is smaller than or
    // equal to packet_retx.
    std::optional<uint32_t> packet_spurious_retx;
    // Total packets sent.
    std::optional<uint32_t> packet_sent;
    // Total packets delivered.
    std::optional<uint32_t> packet_delivered;
    // Total packets delivered with ECE marked. This metric is smaller than or
    // equal to packet_delivered.
    std::optional<uint32_t> packet_delivered_ce;
    // Total bytes lost so far.
    std::optional<uint64_t> data_retx;
    // Total bytes sent so far.
    std::optional<uint64_t> data_sent;
    // Total bytes in write queue but not sent.
    std::optional<uint64_t> data_notsent;
    // Pacing rate of the connection in Bps
    std::optional<uint64_t> pacing_rate;
    // Minimum RTT observed in usec.
    std::optional<uint32_t> min_rtt;
    // Smoothed RTT in usec
    std::optional<uint32_t> srtt;
    // Send congestion window.
    std::optional<uint32_t> congestion_window;
    // Slow start threshold in packets.
    std::optional<uint32_t> snd_ssthresh;
    // Maximum degree of reordering (i.e., maximum number of packets reodered)
    // on the connection.
    std::optional<uint32_t> reordering;
    // Represents the number of recurring retransmissions of the first sequence
    // that is not acknowledged yet.
    std::optional<uint8_t> recurring_retrans;
    // The cumulative time (in usec) that the transport protocol was busy
    // sending data.
    std::optional<uint64_t> busy_usec;
    // The cumulative time (in usec) that the transport protocol was limited by
    // the receive window size.
    std::optional<uint64_t> rwnd_limited_usec;
    // The cumulative time (in usec) that the transport protocol was limited by
    // the send buffer size.
    std::optional<uint64_t> sndbuf_limited_usec;
  };

  enum class Metric {
    kDeliveryRate,
    kIsDeliveryRateAppLimited,
    kPacketRetx,
    kPacketSpuriousRetx,
    kPacketSent,
    kPacketDelivered,
    kPacketDeliveredCE,
    kDataRetx,
    kDataSent,
    kDataNotSent,
    kPacingRate,
    kMinRtt,
    kSrtt,
    kCongestionWindow,
    kSndSsthresh,
    kReordering,
    kRecurringRetrans,
    kBusyUsec,
    kRwndLimitedUsec,
    kSndbufLimitedUsec,
    // Must be last.
    kCount
  };

  explicit PosixWriteEventSink(EventEngine::Endpoint::WriteEventSink sink)
      : requested_metrics_(sink.requested_metrics()),
        requested_events_(sink.requested_events_mask()),
        on_event_(sink.TakeEventCallback()) {}

  static constexpr size_t NumWriteMetrics() {
    return static_cast<size_t>(Metric::kCount);
  }

  static std::vector<size_t> AllWriteMetrics() {
    std::vector<size_t> out;
    out.reserve(NumWriteMetrics());
    for (size_t i = 0; i < NumWriteMetrics(); ++i) {
      out.push_back(i);
    }
    return out;
  }

  static std::optional<size_t> GetMetricKey(absl::string_view name);
  static std::optional<absl::string_view> GetMetricName(size_t key);

  static std::shared_ptr<EventEngine::Endpoint::MetricsSet> GetMetricsSet(
      absl::Span<const size_t> keys) {
    return std::make_shared<MetricsSet>(keys);
  }

  static std::shared_ptr<EventEngine::Endpoint::MetricsSet>
  GetFullMetricsSet() {
    static absl::NoDestructor<std::shared_ptr<FullMetricsSet>> full_metrics_set(
        std::make_shared<FullMetricsSet>());
    return *full_metrics_set;
  }

  void RecordEvent(EventEngine::Endpoint::WriteEvent event,
                   absl::Time timestamp, const ConnectionMetrics& metrics);

 private:
  class MetricsSet : public EventEngine::Endpoint::MetricsSet {
   public:
    explicit MetricsSet(absl::Span<const size_t> keys) {
      for (size_t key : keys) {
        if (key >= static_cast<size_t>(Metric::kCount)) continue;
        metrics_set_.set(static_cast<int>(key));
      }
    }

    bool IsSet(size_t key) const override {
      return key < static_cast<size_t>(Metric::kCount) &&
             metrics_set_.is_set(key);
    }

   private:
    grpc_core::BitSet<static_cast<int>(Metric::kCount)> metrics_set_;
  };

  class FullMetricsSet : public EventEngine::Endpoint::MetricsSet {
   public:
    bool IsSet(size_t key) const override {
      return key < static_cast<size_t>(Metric::kCount);
    }
  };

  std::shared_ptr<EventEngine::Endpoint::MetricsSet> requested_metrics_;
  grpc_event_engine::experimental::EventEngine::Endpoint::WriteEventSet
      requested_events_;
  absl::AnyInvocable<void(internal::WriteEvent, absl::Time,
                          std::vector<EventEngine::Endpoint::WriteMetric>)>
      on_event_;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_WRITE_EVENT_SINK_H
