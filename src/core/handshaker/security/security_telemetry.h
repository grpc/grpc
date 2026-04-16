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

#ifndef GRPC_SRC_CORE_HANDSHAKER_SECURITY_SECURITY_TELEMETRY_H
#define GRPC_SRC_CORE_HANDSHAKER_SECURITY_SECURITY_TELEMETRY_H

#include "src/core/telemetry/instrument.h"
#include "src/core/telemetry/histogram.h"

namespace grpc_core {

class HandshakeTelemetryDomain final : public InstrumentDomain<HandshakeTelemetryDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "security_handshaker";
  GRPC_INSTRUMENT_DOMAIN_LABELS("grpc.security.handshaker.status",
                                "grpc.security.handshaker.error_details",
                                "grpc.security.handshaker.protocol");

  static inline const auto kDuration = RegisterHistogram<ExponentialHistogramShape>(
      "grpc.security.handshaker.duration",
      "Duration of security handshake", "us", 1e6, 20);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_HANDSHAKER_SECURITY_SECURITY_TELEMETRY_H
