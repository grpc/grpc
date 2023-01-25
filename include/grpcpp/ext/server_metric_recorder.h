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
#include <map>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"

#include <grpcpp/impl/sync.h>

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
  /// Records a named resource utilization value in the range [0, 1].
  /// Values outside of the valid range are rejected.
  /// Overrides the stored value when called again with the same name.
  void SetNamedUtilization(std::string name, double value);
  /// Replaces all named resource utilization values. No range validation.
  void SetAllNamedUtilization(std::map<std::string, double> named_utilization);

  /// Clears the server CPU utilization if recorded.
  void ClearCpuUtilization();
  /// Clears the server memory utilization if recorded.
  void ClearMemoryUtilization();
  /// Clears number of queries per second to the server if recorded.
  void ClearQps();
  /// Clears a named utilization value if exists.
  void ClearNamedUtilization(absl::string_view name);

 private:
  // To access GetMetrics().
  friend class grpc::BackendMetricState;
  friend class OrcaService;

  // Returns a pair of backend metrics recorded and the sequence number
  // associated with the current data. Backend metrics will be nullopt if empty.
  // Returned metrics are guaranteed to be identical between two calls if the
  // sequence numbers are identical. Returns {nullopt, seq} if the caller sets
  // `last_seq` and this matches with the current sequence number.
  std::pair<absl::optional<grpc_core::BackendMetricData>, uint64_t> GetMetrics(
      absl::optional<uint64_t> last_seq = absl::nullopt) const;
  // Incremented every time updated.
  std::atomic<uint64_t> seq_{0};
 
  mutable grpc::internal::Mutex mu_;
  std::map<std::string, double> named_utilization_ ABSL_GUARDED_BY(&mu_);
  // Defaults to -1.0 (unset).
  double cpu_utilization_ ABSL_GUARDED_BY(&mu_) = -1.0;
  double mem_utilization_ ABSL_GUARDED_BY(&mu_) = -1.0;
  double qps_ ABSL_GUARDED_BY(&mu_) = -1.0;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_EXT_SERVER_METRIC_RECORDER_H
