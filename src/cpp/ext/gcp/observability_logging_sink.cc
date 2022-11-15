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
#include <utility>

#include "absl/types/optional.h"
#include "google/logging/v2/logging.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include "src/core/lib/gprpp/env.h"
#include "src/cpp/ext/filters/census/open_census_call_tracer.h"

namespace grpc {
namespace internal {

ObservabilityLoggingSink::ObservabilityLoggingSink(
    GcpObservabilityConfig::CloudLogging logging_config, std::string project_id)
    : project_id_(std::move(project_id)) {
  for (auto& client_rpc_event_config : logging_config.client_rpc_events) {
    client_configs_.emplace_back(client_rpc_event_config);
  }
  for (auto& server_rpc_event_config : logging_config.server_rpc_events) {
    server_configs_.emplace_back(server_rpc_event_config);
  }
  std::string endpoint;
  absl::optional<std::string> endpoint_env =
      grpc_core::GetEnv("GOOGLE_CLOUD_CPP_LOGGING_SERVICE_V2_ENDPOINT");
  if (endpoint_env.has_value() && !endpoint_env->empty()) {
    endpoint = std::move(*endpoint_env);
  } else {
    endpoint = "logging.googleapis.com";
  }
  ChannelArguments args;
  // Disable observability for RPCs on this channel
  args.SetInt(GRPC_ARG_ENABLE_OBSERVABILITY, 0);
  // Set keepalive time to 24 hrs to effectively disable keepalive ping, but
  // still enable KEEPALIVE_TIMEOUT to get the TCP_USER_TIMEOUT effect.
  args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 24 * 60 * 60 * 1000 /* 24 hours */);
  args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 20 * 1000 /* 20 seconds */);
  stub_ = google::logging::v2::LoggingServiceV2::NewStub(
      CreateCustomChannel(endpoint, GoogleDefaultCredentials(), args));
  absl::optional<std::string> authority_env =
      grpc_core::GetEnv("GOOGLE_CLOUD_CPP_LOGGING_SERVICE_V2_ENDPOINT");
  if (authority_env.has_value() && !authority_env->empty()) {
    authority_ = std::move(*endpoint_env);
  }
}

LoggingSink::Config ObservabilityLoggingSink::FindMatch(
    bool is_client, absl::string_view path) {
  size_t pos = path.find('/');
  if (pos == absl::string_view::npos) {
    // bad path - did not find '/'
    return LoggingSink::Config(0, 0);
  }
  absl::string_view service =
      path.substr(0, pos);  // service name is before the '/'
  absl::string_view method =
      path.substr(pos + 1);  // method name starts after the '/'
  const auto& configs = is_client ? client_configs_ : server_configs_;
  for (const auto& config : configs) {
    for (const auto& config_method : config.parsed_methods) {
      if ((config_method.service == "*") ||
          ((service == config_method.service) &&
           ((config_method.method == "*") ||
            (method == config_method.method)))) {
        if (config.exclude) {
          return LoggingSink::Config(0, 0);
        }
        return LoggingSink::Config(config.max_metadata_bytes,
                                   config.max_message_bytes);
      }
    }
  }
  return LoggingSink::Config(0, 0);
}

void ObservabilityLoggingSink::LogEntry(Entry entry) {
  struct CallContext {
    ClientContext context;
    google::logging::v2::WriteLogEntriesRequest request;
    google::logging::v2::WriteLogEntriesResponse response;
  };
  // TODO(yashykt): Implement batching so that we can batch a bunch of log
  // entries into a single entry. Also, set a reasonable deadline on the
  // context.
  CallContext* call = new CallContext;
  stub_->async()->WriteLogEntries(&(call->context), &(call->request),
                                  &(call->response), [call](Status status) {
                                    if (!status.ok()) {
                                      // TODO(yashykt): Log the contents of the
                                      // request on a failure.
                                    }
                                    delete call;
                                  });
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
