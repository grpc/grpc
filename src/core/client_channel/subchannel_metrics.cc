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

#include "src/core/client_channel/subchannel_metrics.h"

#include "src/core/telemetry/instrument.h"

namespace grpc_core {

SubchannelMetricsDomainAttempts::CounterHandle
    SubchannelMetricsDomainAttempts::kConnectionAttemptsSucceeded =
        SubchannelMetricsDomainAttempts::RegisterCounter(
            "grpc.subchannel.connection_attempts_succeeded",
            "Number of successful connection attempts.", "attempt");

SubchannelMetricsDomainAttempts::CounterHandle
    SubchannelMetricsDomainAttempts::kConnectionAttemptsFailed =
        SubchannelMetricsDomainAttempts::RegisterCounter(
            "grpc.subchannel.connection_attempts_failed",
            "Number of failed connection attempts.", "attempt");

SubchannelMetricsDomainDisconnections::CounterHandle
    SubchannelMetricsDomainDisconnections::kDisconnections =
        SubchannelMetricsDomainDisconnections::RegisterCounter(
            "grpc.subchannel.disconnections",
            "Number of times the selected subchannel becomes disconnected.",
            "disconnection");

SubchannelConnectionsDomainOpenConnections::UpDownCounterHandle
    SubchannelConnectionsDomainOpenConnections::kOpenConnections =
        SubchannelConnectionsDomainOpenConnections::RegisterUpDownCounter(
            "grpc.subchannel.open_connections",
            "Number of open subchannel connections.", "connection");

}  // namespace grpc_core
