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

#ifndef GRPC_SRC_CPP_EXT_GCP_OBSERVABILITY_LOGGING_SINK_H
#define GRPC_SRC_CPP_EXT_GCP_OBSERVABILITY_LOGGING_SINK_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/struct.pb.h>

#include "absl/base/call_once.h"
#include "absl/strings/string_view.h"
#include "google/logging/v2/logging.grpc.pb.h"

#include "src/cpp/ext/filters/logging/logging_sink.h"
#include "src/cpp/ext/gcp/observability_config.h"

namespace grpc {
namespace internal {

// Interface for a logging sink that will be used by the logging filter.
class ObservabilityLoggingSink : public LoggingSink {
 public:
  ObservabilityLoggingSink(GcpObservabilityConfig::CloudLogging logging_config,
                           std::string project_id,
                           std::map<std::string, std::string> labels);

  ~ObservabilityLoggingSink() override = default;

  LoggingSink::Config FindMatch(bool is_client, absl::string_view service,
                                absl::string_view method) override;

  void LogEntry(Entry entry) override;

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

  std::vector<Configuration> client_configs_;
  std::vector<Configuration> server_configs_;
  std::string project_id_;
  std::string authority_;
  std::vector<std::pair<std::string, std::string>> labels_;
  absl::once_flag once_;
  std::unique_ptr<google::logging::v2::LoggingServiceV2::StubInterface> stub_;
};

// Exposed for just for testing purposes
void EntryToJsonStructProto(LoggingSink::Entry entry,
                            ::google::protobuf::Struct* json_payload);

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_GCP_OBSERVABILITY_LOGGING_SINK_H
