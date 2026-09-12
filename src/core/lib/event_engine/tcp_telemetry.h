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

#include "src/core/telemetry/histogram.h"
#include "src/core/telemetry/instrument.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class TcpTelemetryDomain final : public InstrumentDomain<TcpTelemetryDomain> {
 public:
  using Backend = HighContentionBackend;
  static constexpr absl::string_view kName = "tcp_connection_metrics";
  GRPC_INSTRUMENT_DOMAIN_LABELS("network.local.address", "network.local.port",
                                "network.remote.address", "network.remote.port",
                                "is_control_endpoint", "is_client");

  static HistogramHandle<ExponentialInt64HistogramShape> kMinRtt;
  static HistogramHandle<ExponentialInt64HistogramShape> kDeliveryRate;
  static CounterHandle kPacketsSent;
  static CounterHandle kPacketsRetransmitted;
  static CounterHandle kPacketsSpuriousRetransmitted;
  static CounterHandle kRecurringRetransmits;
  static CounterHandle kBytesSent;
  static CounterHandle kBytesRetransmitted;
  static UpDownCounterHandle kConnectionCount;
  static CounterHandle kSyscallWrites;
  static HistogramHandle<ExponentialInt64HistogramShape> kWriteSize;
  static CounterHandle kSyscallReads;
  static HistogramHandle<ExponentialInt64HistogramShape> kReadSize;
  static HistogramHandle<ExponentialInt64HistogramShape> kSenderLatency;
  static HistogramHandle<ExponentialInt64HistogramShape> kTransferLatency1k;
  static HistogramHandle<ExponentialInt64HistogramShape> kTransferLatency8k;
  static HistogramHandle<ExponentialInt64HistogramShape> kTransferLatency64k;
  static HistogramHandle<ExponentialInt64HistogramShape> kTransferLatency256k;
  static HistogramHandle<ExponentialInt64HistogramShape> kTransferLatency2m;
  static HistogramHandle<ExponentialInt64HistogramShape> kTransferLatency8m;
};

};  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_TCP_TELEMETRY_H
