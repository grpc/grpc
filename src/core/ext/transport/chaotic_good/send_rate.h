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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SEND_RATE_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SEND_RATE_H

#include <cstdint>
#include <optional>

#include "src/core/channelz/property_list.h"

namespace grpc_core {
namespace chaotic_good {

class SendRate {
 public:
  explicit SendRate(
      double initial_rate = 0 /* <=0 ==> not set, bytes per nanosecond */)
      : current_rate_(initial_rate) {}

  struct NetworkSend {
    uint64_t start_time;
    uint64_t bytes;
  };
  struct NetworkMetrics {
    std::optional<uint64_t> rtt_usec;
    std::optional<double> bytes_per_nanosecond;
  };
  // Called when Scheduler enqueues bytes to the reader.
  void EnqueueToReader(const uint64_t bytes, const uint64_t now) {
    timestamps_.last_scheduled_time = now;
    queued_bytes_.network_outstanding_bytes += bytes;
    queued_bytes_.reader_outstanding_bytes += bytes;
  }
  // Called when the Endpoint dequeues all the queued bytes from the reader.
  void DequeueFromReader(const uint64_t now) {
    timestamps_.last_reader_dequeued_time = now;
    queued_bytes_.endpoint_outstanding_bytes =
        queued_bytes_.reader_outstanding_bytes;
    queued_bytes_.reader_outstanding_bytes = 0;
  }
  // Called when the PromiseEndpoint::Write returns.
  void FinishEndpointWrite() { queued_bytes_.endpoint_outstanding_bytes = 0; }
  void SetNetworkMetrics(const std::optional<NetworkSend>& network_send,
                         const NetworkMetrics& metrics);
  bool IsRateMeasurementStale() const;
  channelz::PropertyList ChannelzProperties() const;
  void PerformRateProbe() { last_rate_measurement_ = Timestamp::Now(); }

  struct Timestamps {
    // Time at which data was last scheduled on the endpoint. This is the time
    // at which reader_outstanding_bytes was updated.
    uint64_t last_scheduled_time = 0;
    // Time at which data was last dequeued from the reader. This is the time at
    // which endpoint_outstanding_bytes was updated.
    uint64_t last_reader_dequeued_time = 0;
    // Time at which the currently measured network send started.
    uint64_t network_send_started_time = 0;
  };

  struct DeliveryData {
    // Time in seconds of the time that a byte sent now would be received at the
    // peer.
    double start_time;
    // The rate of bytes per second that a channel is expected to send.
    double bytes_per_second;
    // Bytes queued in different stages of the pipeline.
    struct QueuedBytes {
      // Tracks the bytes scheduled on the frames_ vector of the Data Endpoint's
      // reader.
      uint64_t reader_outstanding_bytes = 0;
      // Tracks the bytes being written to the TCP socket (via a sendmsg call
      // downstream of PromiseEndpoint::Write).
      uint64_t endpoint_outstanding_bytes = 0;
      // Tracks the unsent data in the TCP socket, updated every 100ms.
      uint64_t network_outstanding_bytes = 0;
    } queued_bytes;
    struct RelativeTimestamps {
      // The time in seconds since the last time when data was last scheduled on
      // the endpoint.
      double last_scheduled_time;
      // Time in seconds since the last time data was dequeued from the reader.
      double last_reader_dequeued_time;
    } timestamps;
  };
  DeliveryData GetDeliveryData(uint64_t current_time) const;

 private:
  Timestamps timestamps_;
  DeliveryData::QueuedBytes queued_bytes_;
  double current_rate_;      // bytes per nanosecond
  uint64_t rtt_usec_ = 0.0;  // nanoseconds
  Timestamp last_rate_measurement_ = Timestamp::ProcessEpoch();
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SEND_RATE_H
