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

#include <map>
#include <memory>

#include <grpcpp/support/string_ref.h>

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
  virtual void SetCpuUtilization(double value) = 0;
  /// Records the server memory utilization in the range [0, 1].
  /// Values outside of the valid range are rejected.
  /// Overrides the stored value when called again with a valid value.
  virtual void SetMemoryUtilization(double value) = 0;
  /// Records number of queries per second to the server in the range [0, infy).
  /// Values outside of the valid range are rejected.
  /// Overrides the stored value when called again with a valid value.
  virtual void SetQps(double value) = 0;
  /// Records a named resource utilization value in the range [0, 1].
  /// Values outside of the valid range are rejected.
  /// Overrides the stored value when called again with the same name.
  /// The name string should remain valid while this utilization remains
  /// in this recorder. It is assumed that strings are common names that are
  /// global constants.
  virtual void SetNamedUtilization(string_ref name, double value) = 0;
  /// Replaces all named resource utilization values. No range validation.
  /// The name strings should remain valid while utilization values remain
  /// in this recorder. It is assumed that strings are common names that are
  /// global constants.
  virtual void SetAllNamedUtilization(std::map<string_ref, double> named_utilization) = 0;
  /// Clears the server CPU utilization if recorded.
  virtual void ClearCpuUtilization() = 0;
  /// Clears the server memory utilization if recorded.
  virtual void ClearMemoryUtilization() = 0;
  /// Clears number of queries per second to the server if recorded.
  virtual void ClearQps() = 0;
  /// Clears a named utilization value if exists.
  virtual void ClearNamedUtilization(string_ref name) = 0;
};

std::unique_ptr<ServerMetricRecorder> CreateServerMetricRecorder();

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_EXT_SERVER_METRIC_RECORDER_H
