//
//
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
//
//

#ifndef GRPC_SRC_CORE_TSI_TELEMETRY_HANDSHAKER_DOMAIN_H
#define GRPC_SRC_CORE_TSI_TELEMETRY_HANDSHAKER_DOMAIN_H

#include "src/core/telemetry/histogram.h"
#include "src/core/telemetry/instrument.h"

class HandshakerDomain : public grpc_core::InstrumentDomain<HandshakerDomain> {
 public:
  enum class Protocol { kSsl = 0, kUndefined = -1 };

  static constexpr absl::string_view kName = "handshaker";
  using Backend = grpc_core::HighContentionBackend;
  // Define labels of interest
  GRPC_INSTRUMENT_DOMAIN_LABELS("grpc.security.handshaker.status",
                                "grpc.security.handshaker.error_details",
                                "grpc.security.handshaker.protocol");

  // Register Metrics
  static inline const auto kHandshakeDuration =
      RegisterHistogram<grpc_core::ExponentialHistogramShape>(
          "grpc.security.handshaker.duration",
          "Duration of the handshake in microseconds", "{us}", 1 << 24, 100);
};

#endif  // GRPC_SRC_CORE_TSI_TELEMETRY_HANDSHAKER_DOMAIN_H