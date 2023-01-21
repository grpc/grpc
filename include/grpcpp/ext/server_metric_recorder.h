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

#ifndef GRPCPP_EXT_SERVER_METRIC_RECORDER_H
#define GRPCPP_EXT_SERVER_METRIC_RECORDER_H

#include <atomic>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace grpc_core {
struct BackendMetricData;
}  // namespace grpc_core

namespace grpc {
class BackendMetricState;

namespace experimental {

/// Records server wide metrics to be reported to the client.
/// Server implementation creates an instance and reports server metrics to it,
/// and then passes it to
/// ServerBuilder::experimental_type::EnableCallMetricRecording or
/// experimental::OrcaService that read metrics to include in the report.
class ServerMetricRecorder {
 public:
  /// Records the server CPU utilization in the range [0, 1].
  /// Values outside of the valid range are rejected.
  /// Overrides the stored value when called again with a valid value.
  void SetCpuUtilization(double value);
  /// Records the server memory utilization in the range [0, 1].
  /// Values outside of the valid range are rejected.
  /// Overrides the stored value when called again with a valid value.
  void SetMemoryUtilization(double value);
  /// Records number of queries per second to the server in the range [0, infy).
  /// Values outside of the valid range are rejected.
  /// Overrides the stored value when called again with a valid value.
  void SetQps(double value);

  /// Clears the server CPU utilization if recorded.
  void ClearCpuUtilization();
  /// Clears the server memory utilization if recorded.
  void ClearMemoryUtilization();
  /// Clears number of queries per second to the server if recorded.
  void ClearQps();

 private:
  // To access GetMetrics().
  friend class grpc::BackendMetricState;
  friend class OrcaService;

  // Only populates fields in `data` that this has recorded metrics.
  grpc_core::BackendMetricData GetMetrics() const;

  // Defaults to -1.0 (unset).
  std::atomic<double> cpu_utilization_{-1.0};
  std::atomic<double> mem_utilization_{-1.0};
  std::atomic<double> qps_{-1.0};
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_EXT_SERVER_METRIC_RECORDER_H
