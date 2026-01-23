//
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
//

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_METRICS_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_METRICS_H

#include "src/core/telemetry/instrument.h"

namespace grpc_core {

class SubchannelMetricsDomainAttempts final
    : public InstrumentDomain<SubchannelMetricsDomainAttempts> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "subchannel";
  static constexpr auto kLabels =
      Labels("grpc.target", "grpc.lb.backend_service", "grpc.lb.locality");

  static inline const auto kConnectionAttemptsSucceeded =
      RegisterCounter("grpc.subchannel.connection_attempts_succeeded",
                      "Number of successful connection attempts.", "attempt");
  static inline const auto kConnectionAttemptsFailed =
      RegisterCounter("grpc.subchannel.connection_attempts_failed",
                      "Number of failed connection attempts.", "attempt");
};

class SubchannelMetricsDomainDisconnections final
    : public InstrumentDomain<SubchannelMetricsDomainDisconnections> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "subchannel";
  static constexpr auto kLabels =
      Labels("grpc.target", "grpc.lb.backend_service",
             "grpc.lb.locality", "grpc.disconnect_error");

  static inline const auto kDisconnections = RegisterCounter(
      "grpc.subchannel.disconnections",
      "Number of times the selected subchannel becomes disconnected.",
      "disconnection");
};

class SubchannelConnectionsDomainOpenConnections final
    : public InstrumentDomain<SubchannelConnectionsDomainOpenConnections> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "subchannel";
  static constexpr auto kLabels =
      Labels("grpc.target", "grpc.security_level", "grpc.lb.backend_service",
             "grpc.lb.locality");

  static inline const auto kOpenConnections = RegisterUpDownCounter(
      "grpc.subchannel.open_connections",
      "Number of open subchannel connections.", "connection");
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_METRICS_H