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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/gcp/observability_logging_sink.h"

#include <stddef.h>

#include <algorithm>

#include "observability_logging_sink.h"

namespace grpc {
namespace internal {

ObservabilityLoggingSink::ObservabilityLoggingSink(
    GcpObservabilityConfig::CloudLogging logging_config) {
  for (auto& client_rpc_event_config : logging_config.client_rpc_events) {
    client_configs.emplace_back(client_rpc_event_config);
  }
  for (auto& server_rpc_event_config : logging_config.server_rpc_events) {
    server_configs.emplace_back(server_rpc_event_config);
  }
}

LoggingSink::ParsedConfig ObservabilityLoggingSink::Parse(
    bool is_client, absl::string_view path) {
  size_t pos = path.find('/');
  if (pos == absl::string_view::npos) {
    // bad path - did not find '/'
    return LoggingSink::ParsedConfig(0, 0);
  }
  absl::string_view service =
      path.substr(0, pos);  // service name is before the '/'
  absl::string_view method =
      path.substr(pos + 1);  // method name starts after the '/'
  const auto& configs = is_client ? client_configs : server_configs;
  for (const auto& config : configs) {
    for (const auto& config_method : config.parsed_methods) {
      if ((config_method.service == "*") ||
          ((service == config_method.service) &&
           ((config_method.method == "*") ||
            (method == config_method.method)))) {
        if (config.exclude) {
          return LoggingSink::ParsedConfig(0, 0);
        }
        return LoggingSink::ParsedConfig(config.max_metadata_bytes,
                                         config.max_message_bytes);
      }
    }
  }
  return LoggingSink::ParsedConfig(0, 0);
}

ObservabilityLoggingSink::Configuration::Configuration(
    const GcpObservabilityConfig::CloudLogging::RpcEventConfiguration&
        rpc_event_config)
    : exclude(rpc_event_config.exclude),
      max_metadata_bytes(rpc_event_config.max_metadata_bytes),
      max_message_bytes(rpc_event_config.max_message_bytes) {
  for (auto& parsed_method : rpc_event_config.parsed_methods) {
    parsed_methods.emplace_back(ParsedMethod{
        std::string(parsed_method.service), std::string(parsed_method.method)});
  }
}

}  // namespace internal
}  // namespace grpc