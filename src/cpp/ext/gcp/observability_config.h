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

#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"

namespace grpc {
namespace internal {

struct GcpObservabilityConfig {
  struct CloudLogging {
    bool enabled = false;

    static const grpc_core::JsonLoaderInterface* JsonLoader(
        const grpc_core::JsonArgs&) {
      static const auto* loader =
          grpc_core::JsonObjectLoader<CloudLogging>()
              .OptionalField("enabled", &CloudLogging::enabled)
              .Finish();
      return loader;
    }
  };

  struct CloudMonitoring {
    bool enabled = false;

    static const grpc_core::JsonLoaderInterface* JsonLoader(
        const grpc_core::JsonArgs&) {
      static const auto* loader =
          grpc_core::JsonObjectLoader<CloudMonitoring>()
              .OptionalField("enabled", &CloudMonitoring::enabled)
              .Finish();
      return loader;
    }
  };

  struct CloudTrace {
    bool enabled = false;

    static const grpc_core::JsonLoaderInterface* JsonLoader(
        const grpc_core::JsonArgs&) {
      static const auto* loader =
          grpc_core::JsonObjectLoader<CloudTrace>()
              .OptionalField("enabled", &CloudTrace::enabled)
              .Finish();
      return loader;
    }
  };

  CloudLogging cloud_logging;
  CloudMonitoring cloud_monitoring;
  CloudTrace cloud_trace;
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
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_CONFIG_H
