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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_TCP_TELEMETRY_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_TCP_TELEMETRY_H

#include <cstdint>

#include "src/core/telemetry/histogram.h"
#include "src/core/telemetry/instrument.h"

namespace grpc_core {

class TcpTelemetryDomain final : public InstrumentDomain<TcpTelemetryDomain> {
 public:
  using Backend = HighContentionBackend;
  static constexpr absl::string_view kName = "tcp_connection_metrics";
  GRPC_INSTRUMENT_DOMAIN_LABELS("network.local.address", "network.local.port",
                                "network.remote.address", "network.remote.port",
                                "is_control_endpoint");

  static inline const auto kMinRtt =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.min_rtt",
          "Minimum round trip time of a connection in microseconds", "{us}",
          1 << 24, 100);  // Max bucket is 16 seconds.
  static inline const auto kDeliveryRate =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.delivery_rate",
          "TCP's most recent measure of the connection's "
          "\"non-app-limited\" throughput.",
          "By/s", int64_t{1} << 34, 100);  // Max bucket is 16 GiB/s.
  static inline const auto kPacketsSent = RegisterCounter(
      "grpc.tcp.packets_sent",
      "Total packets sent by TCP including retransmissions and spurious "
      "retransmissions.",
      "{packet}");
  static inline const auto kPacketsRetransmitted = RegisterCounter(
      "grpc.tcp.packets_retransmitted",
      "Total packets sent by TCP except those sent for the first time.",
      "{packet}");
  static inline const auto kPacketsSpuriousRetransmitted = RegisterCounter(
      "grpc.tcp.packets_spurious_retransmitted",
      "Total packets retransmitted by TCP that were later found to be "
      "unnecessary.",
      "{packet}");
  static inline const auto kRecurringRetransmits = RegisterCounter(
      "grpc.tcp.recurring_retransmits",
      "The number of times the latest TCP packet was retransmitted due to "
      "expiration of TCP retransmission timer (RTO), and not acknowledged at "
      "the time the connection was closed.",
      "{packet}");
  static inline const auto kBytesSent = RegisterCounter(
      "grpc.tcp.bytes_sent",
      "Total bytes sent by TCP including retransmissions and spurious "
      "retransmissions.",
      "By");
  static inline const auto kBytesRetransmitted = RegisterCounter(
      "grpc.tcp.bytes_retransmitted",
      "Total bytes sent by TCP except those sent for the first time.", "By");
  static inline const auto kConnectionCount = RegisterUpDownCounter(
      "grpc.tcp.connection_count", "Number of active TCP connections.",
      "{connection}");
  static inline const auto kSyscallWrites = RegisterCounter(
      "grpc.tcp.syscall_writes",
      "The number of times we invoked the sendmsg (or sendmmsg) syscall and "
      "wrote data to the TCP socket.",
      "{syscall}");
  static inline const auto kWriteSize =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.write_size",
          "The number of bytes offered to each syscall_write.", "By", 8 << 20,
          20);
  static inline const auto kSyscallReads = RegisterCounter(
      "grpc.tcp.syscall_reads",
      "The number of times we invoked the recvmsg (or recvmmsg or zero copy "
      "getsockopt) syscall and read data from the TCP socket.",
      "{syscall}");
  static inline const auto kReadSize =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.read_size",
          "The number of bytes received by each syscall_read.", "By", 8 << 20,
          20);
  static inline const auto kSenderLatency =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.sender_latency",
          "Time taken by the TCP socket to write the first byte of a write "
          "onto the NIC.",
          "us", 1e6, 20);
  static inline const auto kTransferLatency1k =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.transfer_latency_1k",
          "Time taken to transmit the first 1024 bytes of a write.", "us", 1e6,
          20);
  static inline const auto kTransferLatency8k =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.transfer_latency_8k",
          "Time taken to transmit the first 8196 bytes of a write.", "us", 1e6,
          20);
  static inline const auto kTransferLatency64k =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.transfer_latency_64k",
          "Time taken to transmit the first 65536 bytes of a write.", "us", 1e6,
          20);
  static inline const auto kTransferLatency256k =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.transfer_latency_256k",
          "Time taken to transmit the first 262144 bytes of a write.", "us",
          1e6, 20);
  static inline const auto kTransferLatency2m =
      RegisterHistogram<ExponentialHistogramShape>(
          "grpc.tcp.transfer_latency_2m",
          "Time taken to transmit the first 2097152 bytes of a write.", "us",
          1e6, 20);
};

};  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_TCP_TELEMETRY_H
