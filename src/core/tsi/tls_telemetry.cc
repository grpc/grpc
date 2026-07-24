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

#include "src/core/tsi/tls_telemetry.h"

#include "src/core/telemetry/instrument.h"

namespace grpc_core {

TlsClientHandshakeTelemetryDomain::CounterHandle
    TlsClientHandshakeTelemetryDomain::kHandshakes = RegisterCounter(
        "grpc.client.tls.handshakes",
        "Total number of client-side TLS handshakes", "{handshake}");

TlsServerHandshakeTelemetryDomain::CounterHandle
    TlsServerHandshakeTelemetryDomain::kHandshakes = RegisterCounter(
        "grpc.server.tls.handshakes",
        "Total number of server-side TLS handshakes", "{handshake}");

}  // namespace grpc_core