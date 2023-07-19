//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CPP_EXT_OTEL_OTEL_PLUGIN_H
#define GRPC_SRC_CPP_EXT_OTEL_OTEL_PLUGIN_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <memory>

#include "absl/strings/string_view.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace grpc {
namespace internal {

struct OTelPluginState {
  struct Client {
    struct Attempt {
      std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> started;
      std::unique_ptr<opentelemetry::metrics::Histogram<double>> duration;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          sent_total_compressed_message_size;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          rcvd_total_compressed_message_size;
    } attempt;
  } client;
  struct Server {
    struct Call {
      std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> started;
      std::unique_ptr<opentelemetry::metrics::Histogram<double>> duration;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          sent_total_compressed_message_size;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          rcvd_total_compressed_message_size;
    } call;
  } server;
};

const struct OTelPluginState& OTelPluginState();

void RegisterOpenTelemetryPlugin();

absl::string_view OTelMethodKey();
absl::string_view OTelStatusKey();

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_OTEL_PLUGIN_H
