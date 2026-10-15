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

#ifndef GRPC_SRC_CORE_TSI_TLS_TELEMETRY_H
#define GRPC_SRC_CORE_TSI_TLS_TELEMETRY_H

#include "src/core/telemetry/instrument.h"

namespace grpc_core {

class TlsClientHandshakeTelemetryDomain final
    : public InstrumentDomain<TlsClientHandshakeTelemetryDomain> {
 public:
  GRPC_INSTRUMENT_DOMAIN_LABELS("grpc.tls.handshake.result", "grpc.target",
                                "grpc.tls.handshake.resumed",
                                "grpc.lb.locality", "grpc.lb.backend_service");
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "tls_client_security_handshaker";

  static CounterHandle kHandshakes;
};

class TlsServerHandshakeTelemetryDomain final
    : public InstrumentDomain<TlsServerHandshakeTelemetryDomain> {
 public:
  GRPC_INSTRUMENT_DOMAIN_LABELS("grpc.tls.handshake.result",
                                "grpc.tls.handshake.resumed");
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "tls_server_security_handshaker";

  static CounterHandle kHandshakes;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TSI_TLS_TELEMETRY_H
