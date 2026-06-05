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

#include "src/core/lib/event_engine/tcp_telemetry.h"

#include <cstdint>

#include "src/core/telemetry/histogram.h"
#include "src/core/telemetry/instrument.h"

namespace grpc_core {

// Telemetry domain static handle definitions and registrations (Dynamic on
// Load)
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kMinRtt =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.min_rtt",
            "Minimum round trip time of a connection in microseconds", "{us}",
            1 << 24, 100);  // Max bucket is 16 seconds.
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kDeliveryRate =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.delivery_rate",
            "TCP's most recent measure of the connection's "
            "\"non-app-limited\" throughput.",
            "By/s", int64_t{1} << 34, 100);  // Max bucket is 16 GiB/s.
TcpTelemetryDomain::CounterHandle TcpTelemetryDomain::kPacketsSent =
    TcpTelemetryDomain::RegisterCounter(
        "grpc.tcp.packets_sent",
        "Total packets sent by TCP including retransmissions and spurious "
        "retransmissions.",
        "{packet}");
TcpTelemetryDomain::CounterHandle TcpTelemetryDomain::kPacketsRetransmitted =
    TcpTelemetryDomain::RegisterCounter(
        "grpc.tcp.packets_retransmitted",
        "Total packets sent by TCP except those sent for the first time.",
        "{packet}");
TcpTelemetryDomain::CounterHandle
    TcpTelemetryDomain::kPacketsSpuriousRetransmitted =
        TcpTelemetryDomain::RegisterCounter(
            "grpc.tcp.packets_spurious_retransmitted",
            "Total packets retransmitted by TCP that were later found to be "
            "unnecessary.",
            "{packet}");
TcpTelemetryDomain::CounterHandle TcpTelemetryDomain::kRecurringRetransmits =
    TcpTelemetryDomain::RegisterCounter(
        "grpc.tcp.recurring_retransmits",
        "The number of times the latest TCP packet was retransmitted due to "
        "expiration of TCP retransmission timer (RTO), and not acknowledged at "
        "the time the connection was closed.",
        "{packet}");
TcpTelemetryDomain::CounterHandle TcpTelemetryDomain::kBytesSent =
    TcpTelemetryDomain::RegisterCounter(
        "grpc.tcp.bytes_sent",
        "Total bytes sent by TCP including retransmissions and spurious "
        "retransmissions.",
        "By");
TcpTelemetryDomain::CounterHandle TcpTelemetryDomain::kBytesRetransmitted =
    TcpTelemetryDomain::RegisterCounter(
        "grpc.tcp.bytes_retransmitted",
        "Total bytes sent by TCP except those sent for the first time.", "By");
TcpTelemetryDomain::UpDownCounterHandle TcpTelemetryDomain::kConnectionCount =
    TcpTelemetryDomain::RegisterUpDownCounter(
        "grpc.tcp.connection_count", "Number of active TCP connections.",
        "{connection}");
TcpTelemetryDomain::CounterHandle TcpTelemetryDomain::kSyscallWrites =
    TcpTelemetryDomain::RegisterCounter(
        "grpc.tcp.syscall_writes",
        "The number of times we invoked the sendmsg (or sendmmsg) syscall and "
        "wrote data to the TCP socket.",
        "{syscall}");
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kWriteSize =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.write_size",
            "The number of bytes offered to each syscall_write.", "By", 8 << 20,
            20);
TcpTelemetryDomain::CounterHandle TcpTelemetryDomain::kSyscallReads =
    TcpTelemetryDomain::RegisterCounter(
        "grpc.tcp.syscall_reads",
        "The number of times we invoked the recvmsg (or recvmmsg or zero copy "
        "getsockopt) syscall and read data from the TCP socket.",
        "{syscall}");
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kReadSize =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.read_size",
            "The number of bytes received by each syscall_read.", "By", 8 << 20,
            20);
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kSenderLatency =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.sender_latency",
            "Time taken by the TCP socket to write the first byte of a write "
            "onto the NIC.",
            "us", 1e6, 20);
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kTransferLatency1k =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.transfer_latency_1k",
            "Time taken to transmit the first 1024 bytes of a write.", "us",
            1e6, 20);
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kTransferLatency8k =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.transfer_latency_8k",
            "Time taken to transmit the first 8196 bytes of a write.", "us",
            1e6, 20);
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kTransferLatency64k =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.transfer_latency_64k",
            "Time taken to transmit the first 65536 bytes of a write.", "us",
            1e6, 20);
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kTransferLatency256k =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.transfer_latency_256k",
            "Time taken to transmit the first 262144 bytes of a write.", "us",
            1e6, 20);
TcpTelemetryDomain::HistogramHandle<ExponentialHistogramShape>
    TcpTelemetryDomain::kTransferLatency2m =
        TcpTelemetryDomain::RegisterHistogram<ExponentialHistogramShape>(
            "grpc.tcp.transfer_latency_2m",
            "Time taken to transmit the first 2097152 bytes of a write.", "us",
            1e6, 20);

}  // namespace grpc_core
