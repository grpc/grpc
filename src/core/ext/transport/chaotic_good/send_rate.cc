// Copyright 2025 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/send_rate.h"

#include "src/core/util/grpc_check.h"
#include "absl/log/log.h"

namespace grpc_core {
namespace chaotic_good {

void SendRate::SetNetworkMetrics(const std::optional<NetworkSend>& network_send,
                                 const NetworkMetrics& metrics) {
  bool updated = false;
  if (metrics.rtt_usec.has_value()) {
    GRPC_CHECK_GE(*metrics.rtt_usec, 0u);
    rtt_usec_ = *metrics.rtt_usec;
    updated = true;
  }
  if (metrics.bytes_per_nanosecond.has_value()) {
    if (*metrics.bytes_per_nanosecond < 0) {
      LOG_EVERY_N_SEC(ERROR, 10)
          << "Negative bytes per nanosecond: " << *metrics.bytes_per_nanosecond;
    } else if (std::isnan(*metrics.bytes_per_nanosecond)) {
      LOG_EVERY_N_SEC(ERROR, 10)
          << "NaN bytes per nanosecond: " << *metrics.bytes_per_nanosecond;
    } else {
      current_rate_ = *metrics.bytes_per_nanosecond;
    }
    updated = true;
  }
  if (network_send.has_value() &&
      network_send->start_time > timestamps_.network_send_started_time) {
    timestamps_.network_send_started_time = network_send->start_time;
    queued_bytes_.network_outstanding_bytes = network_send->bytes;
    updated = true;
  }
  if (updated) last_rate_measurement_ = Timestamp::Now();
}

bool SendRate::IsRateMeasurementStale() const {
  return Timestamp::Now() - last_rate_measurement_ > Duration::Seconds(1);
}

// Returns a signed double representing the difference between the two times.
double ToRelativeTime(uint64_t ts, uint64_t now) {
  // Use integer subtraction to avoid rounding errors, getting everything
  // with a zero base of 'now' to maximize precision.
  // Since we have uint64_ts and want a signed double result we need to
  // care about argument ordering to get a valid result.
  return now > ts ? -static_cast<double>(now - ts)
                  : static_cast<double>(ts - now);
}

SendRate::DeliveryData SendRate::GetDeliveryData(uint64_t current_time) const {
  // start time relative to the current time for this send
  double start_time = 0.0;
  if (timestamps_.network_send_started_time != 0 && current_rate_ > 0) {
    const double send_start_time_relative_to_now =
        ToRelativeTime(timestamps_.network_send_started_time, current_time);
    const double predicted_end_time =
        send_start_time_relative_to_now +
        queued_bytes_.network_outstanding_bytes / current_rate_;
    if (predicted_end_time > start_time) start_time = predicted_end_time;
  }
  double relative_last_scheduled_time =
      ToRelativeTime(timestamps_.last_scheduled_time, current_time);
  double relative_last_reader_dequeued_time =
      ToRelativeTime(timestamps_.last_reader_dequeued_time, current_time);
  if (current_rate_ <= 0) {
    return DeliveryData{
        (start_time + rtt_usec_ * 500.0) * 1e-9, 1e14, queued_bytes_,
        DeliveryData::RelativeTimestamps{relative_last_scheduled_time,
                                         relative_last_reader_dequeued_time}};
  } else {
    return DeliveryData{
        (start_time + rtt_usec_ * 500.0) * 1e-9, current_rate_ * 1e9,
        queued_bytes_,
        DeliveryData::RelativeTimestamps{relative_last_scheduled_time,
                                         relative_last_reader_dequeued_time}};
  }
}

channelz::PropertyList SendRate::ChannelzProperties() const {
  channelz::PropertyList obj;
  if (timestamps_.network_send_started_time != 0) {
    obj.Set("network_send_started_time", timestamps_.network_send_started_time)
        .Set("network_outstanding_bytes",
             queued_bytes_.network_outstanding_bytes)
        .Set("endpoint_outstanding_bytes",
             queued_bytes_.endpoint_outstanding_bytes)
        .Set("reader_outstanding_bytes",
             queued_bytes_.reader_outstanding_bytes);
  }
  return obj.Set("current_rate", current_rate_)
      .Set("rtt", rtt_usec_)
      .Set("last_rate_measurement", last_rate_measurement_);
}

}  // namespace chaotic_good
}  // namespace grpc_core
