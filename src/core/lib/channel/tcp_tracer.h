//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_CHANNEL_TCP_TRACER_H
#define GRPC_SRC_CORE_LIB_CHANNEL_TCP_TRACER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "absl/time/time.h"
#include "absl/types/optional.h"

namespace grpc_core {

// Interface for TCP tracer implementations. Created by CallTracerInterface.
class TcpTracerInterface {
 public:
  enum class Type {
    kUnknown = 0,
    // When the sendmsg system call or its variants returned for the traced byte
    // offset.
    kSendMsg,
    // When the traced byte offset is enqueued in kernel schedulers (aka,
    // qdiscs). There can be multiple schedulers.
    kScheduled,
    // When the traced byte offset is handed over to the NIC.
    kSent,
    // When the acknowledgement for the traced byte offset was received.
    kAcked,
    // When the connection is closed. This is not associated with a byte offset.
    kClosed,
  };

  struct ConnectionMetrics {
    // Congestion control name.
    std::string congestion_ctrl;
    // Delivery rate in Bps.
    absl::optional<uint64_t> delivery_rate;
    // Total bytes retransmitted so far.
    absl::optional<uint64_t> data_retx;
    // Total bytes sent so far.
    absl::optional<uint64_t> data_sent;
    // Total packets lost so far.
    // Includes lost or spuriously retransmitted packets.
    absl::optional<uint32_t> packet_retx;
    // Total packets spuriously retransmitted so far.
    absl::optional<uint32_t> packet_spurious_retx;
    // Total packets sent so far.
    absl::optional<uint32_t> packet_sent;
    // Total packets delivered so far.
    absl::optional<uint32_t> packet_delivered;
    // Total packets delivered so far with ECE marked.
    // This metric is smaller than or equal to packet_delivered.
    absl::optional<uint32_t> packet_delivered_ce;
    // Total bytes in write queue but not sent.
    absl::optional<uint64_t> data_notsent;
    // Minimum RTT observed in usec.
    absl::optional<uint32_t> min_rtt;
    // Smoothed RTT in usec
    absl::optional<uint32_t> srtt;
    // TTL or hop limit of a packet received
    // Only available with ACKED timestamps.
    absl::optional<uint32_t> ttl;
    // Represents the number of recurring retransmissions of
    // the first sequence that is not acknowledged yet.
    absl::optional<uint32_t> recurring_retrans;
    // Network RTT using hardware timestamps (in usec).
    // A value of -1 indicates that net_rtt could not be measured.
    absl::optional<int32_t> net_rtt_usec;
    // Timeout-triggered rehash attempts.
    absl::optional<uint32_t> timeout_rehash;
    // Rehash due to ECN congestion.
    absl::optional<uint32_t> ecn_rehash;
    // Earliest departure time (CLOCK_MONOTONIC).
    // Only available with SCHEDULED and SENT timestamps.
    absl::optional<uint64_t> edt;
    // If the delivery rate is limited by the application, this is set to
    // true.
    absl::optional<bool> is_delivery_rate_app_limited;
    // Pacing rate of the connection in Bps.
    absl::optional<uint64_t> pacing_rate;
    // Send congestion window in packets.
    absl::optional<uint32_t> congestion_window;
    // Maximum degree of reordering (i.e., maximum number of packets reodered)
    // on the connection.
    absl::optional<uint32_t> reordering;
    // Cumulative duration (in usec) that the transport protocol
    // was busy sending time.
    absl::optional<uint64_t> busy_usec;
    // Cumulative duration (in usec) that the transport protocol
    // was limited by the receive window size.
    absl::optional<uint64_t> rwnd_limited_usec;
    // Cumulative duration (in usec) that the transport protocol
    // was limited by the send buffer size.
    absl::optional<uint64_t> sndbuf_limited_usec;
    // Slow start size threshold in packets.
    // Set to TCP_INFINITE_SSTHRESH when still in slow start.
    absl::optional<uint32_t> snd_ssthresh;
    // The extra time it takes for the receiver to generate the
    // acknowledgement after receiving the last packet. This metric is not
    // cumulative. Only available with ACKED timestamps.
    absl::optional<uint32_t> time_to_ack_usec;
    // Last socket error code. Only populated for CLOSED timestamps.
    absl::optional<uint32_t> socket_errno;
    // Peer's receive window after scaling (tcpi_snd_wnd).
    // Only available with SENDMSG timestamps.
    absl::optional<uint32_t> peer_rwnd;
    // Receive queue drops.
    absl::optional<uint32_t> rcvq_drops;
    // The NIC Rx delay reported by the remote host.
    absl::optional<uint32_t> nic_rx_delay_usec;
  };

  virtual ~TcpTracerInterface() = default;
  virtual void RecordEvent(Type type, absl::Time time, size_t byte_offset,
                           absl::optional<ConnectionMetrics> metrics) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_TCP_TRACER_H
