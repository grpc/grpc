//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_CONFIG_H
#define GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_CONFIG_H

#include <string>

#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"

namespace grpc {
namespace internal {

struct GcpObservabilityConfig {
  struct CloudLogging {
    static const grpc_core::JsonLoaderInterface* JsonLoader(
        const grpc_core::JsonArgs&) {
      static const auto* loader =
          grpc_core::JsonObjectLoader<CloudLogging>().Finish();
      return loader;
    }
  };

  struct CloudMonitoring {
    static const grpc_core::JsonLoaderInterface* JsonLoader(
        const grpc_core::JsonArgs&) {
      static const auto* loader =
          grpc_core::JsonObjectLoader<CloudMonitoring>().Finish();
      return loader;
    }
  };

  struct CloudTrace {
    float sampling_rate = 0;

    static const grpc_core::JsonLoaderInterface* JsonLoader(
        const grpc_core::JsonArgs&) {
      static const auto* loader =
          grpc_core::JsonObjectLoader<CloudTrace>()
              .OptionalField("sampling_rate", &CloudTrace::sampling_rate)
              .Finish();
      return loader;
    }
  };

  absl::optional<CloudLogging> cloud_logging;
  absl::optional<CloudMonitoring> cloud_monitoring;
  absl::optional<CloudTrace> cloud_trace;
  std::string project_id;

  static const grpc_core::JsonLoaderInterface* JsonLoader(
      const grpc_core::JsonArgs&) {
    static const auto* loader =
        grpc_core::JsonObjectLoader<GcpObservabilityConfig>()
            .OptionalField("cloud_logging",
                           &GcpObservabilityConfig::cloud_logging)
            .OptionalField("cloud_monitoring",
                           &GcpObservabilityConfig::cloud_monitoring)
            .OptionalField("cloud_trace", &GcpObservabilityConfig::cloud_trace)
            .OptionalField("project_id", &GcpObservabilityConfig::project_id)
            .Finish();
    return loader;
  }

  // Tries to load the contents of GcpObservabilityConfig from the file located
  // by the value of environment variable `GRPC_OBSERVABILITY_CONFIG_FILE`. If
  // `GRPC_OBSERVABILITY_CONFIG_FILE` is unset, falls back to
  // `GRPC_OBSERVABILITY_CONFIG`.
  static absl::StatusOr<GcpObservabilityConfig> ReadFromEnv();
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_CONFIG_H
