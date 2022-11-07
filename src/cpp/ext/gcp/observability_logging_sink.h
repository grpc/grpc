//
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
//

#ifndef GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_LOGGING_SINK_H
#define GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_LOGGING_SINK_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

#include "src/cpp/ext/filters/logging/logging_sink.h"
#include "src/cpp/ext/gcp/observability_config.h"

namespace grpc {
namespace internal {

// Interface for a logging sink that will be used by the logging filter.
class ObservabilityLoggingSink : public LoggingSink {
 public:
  explicit ObservabilityLoggingSink(
      GcpObservabilityConfig::CloudLogging logging_config);

  LoggingSink::ParsedConfig Parse(bool is_client,
                                  absl::string_view path) override;

 private:
  struct Configuration {
    explicit Configuration(
        const GcpObservabilityConfig::CloudLogging::RpcEventConfiguration&
            rpc_event_config);
    struct ParsedMethod {
      std::string service;
      std::string method;
    };
    std::vector<ParsedMethod> parsed_methods;
    bool exclude = false;
    uint32_t max_metadata_bytes = 0;
    uint32_t max_message_bytes = 0;
  };

  std::vector<Configuration> client_configs;
  std::vector<Configuration> server_configs;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_GCP_OBSERVABILITY_GCP_OBSERVABILITY_LOGGING_SINK_H
