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
namespace {

const std::vector<int64_t>& GetLatencyBuckets() {
  static const auto* buckets = new std::vector<int64_t>{
      0,      10,     50,     100,    300,    600,    800,
      1000,   2000,   3000,   4000,   5000,   6000,   8000,
      10000,  13000,  16000,  20000,  25000,  30000,  40000,
      50000,  65000,  80000,  100000, 130000, 160000, 200000,
      250000, 300000, 400000, 500000, 650000, 800000, 1000000,
      2000000, 5000000, 10000000, 20000000, 50000000, 100000000};
  return *buckets;
}

}  // namespace

TlsClientHandshakeTelemetryDomain::CounterHandle
    TlsClientHandshakeTelemetryDomain::kHandshakes = RegisterCounter(
        "grpc.client.tls.handshakes",
        "Total number of client-side TLS handshakes", "{handshake}");

TlsServerHandshakeTelemetryDomain::CounterHandle
    TlsServerHandshakeTelemetryDomain::kHandshakes = RegisterCounter(
        "grpc.server.tls.handshakes",
        "Total number of server-side TLS handshakes", "{handshake}");

TlsClientPrivateKeyOffloadTelemetryDomain::HistogramHandle<
    ExplicitHistogramShape>
    TlsClientPrivateKeyOffloadTelemetryDomain::kDuration =
        RegisterHistogram<ExplicitHistogramShape>(
            "grpc.client.tls.offload_private_key_signing_duration",
            "EXPERIMENTAL: Measures the duration of the offloaded private key "
            "signing operation.",
            "us", GetLatencyBuckets());

TlsServerPrivateKeyOffloadTelemetryDomain::HistogramHandle<
    ExplicitHistogramShape>
    TlsServerPrivateKeyOffloadTelemetryDomain::kDuration =
        RegisterHistogram<ExplicitHistogramShape>(
            "grpc.server.tls.offload_private_key_signing_duration",
            "EXPERIMENTAL: Measures the duration of the offloaded private key "
            "signing operation.",
            "us", GetLatencyBuckets());

}  // namespace grpc_core