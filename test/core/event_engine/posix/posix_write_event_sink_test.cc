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

#include "src/core/lib/event_engine/posix_engine/posix_write_event_sink.h"

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

namespace grpc_event_engine::experimental {

using WriteMetric = EventEngine::Endpoint::WriteMetric;
using Metric = PosixWriteEventSink::Metric;

TEST(PosixWriteEventSinkTest, MetricsSetTest) {
  auto metrics_set = PosixWriteEventSink::GetMetricsSet({1, 4, 5, 12, 45, 100});
  EXPECT_TRUE(metrics_set->IsSet(1));
  EXPECT_TRUE(metrics_set->IsSet(4));
  EXPECT_TRUE(metrics_set->IsSet(5));
  EXPECT_TRUE(metrics_set->IsSet(12));
  EXPECT_FALSE(metrics_set->IsSet(45));   // out of range
  EXPECT_FALSE(metrics_set->IsSet(100));  // out of range
}

TEST(PosixWriteEventSinkTest, FullMetricsSetTest) {
  auto metrics_set = PosixWriteEventSink::GetFullMetricsSet();
  EXPECT_TRUE(metrics_set->IsSet(1));
  EXPECT_TRUE(metrics_set->IsSet(4));
  EXPECT_TRUE(metrics_set->IsSet(5));
  EXPECT_TRUE(metrics_set->IsSet(12));
  EXPECT_FALSE(metrics_set->IsSet(45));   // out of range
  EXPECT_FALSE(metrics_set->IsSet(100));  // out of range
}

MATCHER_P2(IsWriteMetric, expected_key, expected_value,
           "is a WriteMetric with key " +
               ::testing::PrintToString(expected_key) + " and value " +
               ::testing::PrintToString(expected_value)) {
  return arg.key == expected_key && arg.value == expected_value;
}

TEST(PosixWriteEventSinkTest, AllMetricsReportedTest) {
  std::vector<EventEngine::Endpoint::WriteMetric> write_metrics;
  EventEngine::Endpoint::WriteEventSink sink(
      PosixWriteEventSink::GetFullMetricsSet(),
      {EventEngine::Endpoint::WriteEvent::kSendMsg},
      [&write_metrics](
          EventEngine::Endpoint::WriteEvent event, absl::Time /*timestamp*/,
          std::vector<EventEngine::Endpoint::WriteMetric> metrics) {
        write_metrics = std::move(metrics);
      });
  PosixWriteEventSink posix_write_event_sink(std::move(sink));
  PosixWriteEventSink::ConnectionMetrics conn_metrics;
  conn_metrics.delivery_rate = 1;
  conn_metrics.is_delivery_rate_app_limited = true;
  conn_metrics.packet_retx = 2;
  conn_metrics.packet_spurious_retx = 3;
  conn_metrics.packet_sent = 4;
  conn_metrics.packet_delivered = 5;
  conn_metrics.packet_delivered_ce = 6;
  conn_metrics.data_retx = 7;
  conn_metrics.data_sent = 8;
  conn_metrics.data_notsent = 9;
  conn_metrics.pacing_rate = 10;
  conn_metrics.min_rtt = 11;
  conn_metrics.srtt = 12;
  conn_metrics.congestion_window = 13;
  conn_metrics.snd_ssthresh = 14;
  conn_metrics.reordering = 15;
  conn_metrics.recurring_retrans = 16;
  conn_metrics.busy_usec = 17;
  conn_metrics.rwnd_limited_usec = 18;
  conn_metrics.sndbuf_limited_usec = 19;

  posix_write_event_sink.RecordEvent(
      EventEngine::Endpoint::WriteEvent::kSendMsg, absl::Now(), conn_metrics);

  EXPECT_THAT(
      write_metrics,
      ::testing::ElementsAre(
          IsWriteMetric(static_cast<size_t>(Metric::kDeliveryRate), 1),
          IsWriteMetric(static_cast<size_t>(Metric::kIsDeliveryRateAppLimited),
                        1),
          IsWriteMetric(static_cast<size_t>(Metric::kPacketRetx), 2),
          IsWriteMetric(static_cast<size_t>(Metric::kPacketSpuriousRetx), 3),
          IsWriteMetric(static_cast<size_t>(Metric::kPacketSent), 4),
          IsWriteMetric(static_cast<size_t>(Metric::kPacketDelivered), 5),
          IsWriteMetric(static_cast<size_t>(Metric::kPacketDeliveredCE), 6),
          IsWriteMetric(static_cast<size_t>(Metric::kDataRetx), 7),
          IsWriteMetric(static_cast<size_t>(Metric::kDataSent), 8),
          IsWriteMetric(static_cast<size_t>(Metric::kDataNotSent), 9),
          IsWriteMetric(static_cast<size_t>(Metric::kPacingRate), 10),
          IsWriteMetric(static_cast<size_t>(Metric::kMinRtt), 11),
          IsWriteMetric(static_cast<size_t>(Metric::kSrtt), 12),
          IsWriteMetric(static_cast<size_t>(Metric::kCongestionWindow), 13),
          IsWriteMetric(static_cast<size_t>(Metric::kSndSsthresh), 14),
          IsWriteMetric(static_cast<size_t>(Metric::kReordering), 15),
          IsWriteMetric(static_cast<size_t>(Metric::kRecurringRetrans), 16),
          IsWriteMetric(static_cast<size_t>(Metric::kBusyUsec), 17),
          IsWriteMetric(static_cast<size_t>(Metric::kRwndLimitedUsec), 18),
          IsWriteMetric(static_cast<size_t>(Metric::kSndbufLimitedUsec), 19)));
}

TEST(PosixWriteEventSinkTest, NotRequestedEventsAreNotReported) {
  bool invoked = false;
  EventEngine::Endpoint::WriteEventSink sink(
      PosixWriteEventSink::GetFullMetricsSet(),
      {EventEngine::Endpoint::WriteEvent::kSendMsg},
      [&invoked](EventEngine::Endpoint::WriteEvent /*event*/,
                 absl::Time /*timestamp*/,
                 std::vector<EventEngine::Endpoint::WriteMetric> /*metrics*/) {
        invoked = true;
      });
  PosixWriteEventSink posix_write_event_sink(std::move(sink));

  posix_write_event_sink.RecordEvent(EventEngine::Endpoint::WriteEvent::kAcked,
                                     absl::Now(),
                                     PosixWriteEventSink::ConnectionMetrics());

  EXPECT_FALSE(invoked);
}

}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
