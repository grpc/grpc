/*
 *
 * Copyright 2022 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_BACKEND_METRIC_DATA_H
#define GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_BACKEND_METRIC_DATA_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <map>

#include "src/core/lib/gprpp/sync.h"
#include "absl/strings/string_view.h"

namespace grpc {
class BackendMetricState;
}  // namespace grpc
 
namespace grpc_core {

// Represents backend metrics reported by the backend to the client.
struct BackendMetricData {
  /// CPU utilization expressed as a fraction of available CPU resources.
  double cpu_utilization = -1;
  /// Memory utilization expressed as a fraction of available memory
  /// resources.
  double mem_utilization = -1;
  /// Queries per second to the server.
  double qps = -1;
  /// Application-specific requests cost metrics.  Metric names are
  /// determined by the application.  Each value is an absolute cost
  /// (e.g. 3487 bytes of storage) associated with the request.
  std::map<absl::string_view, double> request_cost;
  /// Application-specific resource utilization metrics.  Metric names
  /// are determined by the application.  Each value is expressed as a
  /// fraction of total resources available.
  std::map<absl::string_view, double> utilization;
};

class BackendMetricProvider {
 public:
  virtual ~BackendMetricProvider() = default;
  // Only populates fields in `data` that this has recorded metrics.
  virtual void GetBackendMetricData(BackendMetricData* data) = 0;
};

class ServerMetricRecorder {
 public:
  // Records the server CPU utilization in the range [0, 1].
  // Values outside of the valid range are rejected.
  // Overrides the stored value when called again with a valid value.
  void SetCpuUtilization(double value);
  // Records the server memory utilization in the range [0, 1].
  // Values outside of the valid range are rejected.
  // Overrides the stored value when called again with a valid value.
  void SetMemUtilization(double value);
  // Records number of queries per second to the server in the range [0, infy).
  // Values outside of the valid range are rejected.
  // Overrides the stored value when called again with a valid value.
  void SetQps(double value);

  // Clears the server CPU utilization if recorded.
  void ClearCpuUtilization();
  // Clears the server memory utilization if recorded.
  void ClearMemUtilization();
  // Clears number of queries per second to the server if recorded.
  void ClearQps();

 private:
  // Only populates fields in `data` that this has recorded metrics.
  void GetMetrics(grpc_core::BackendMetricData* data);

  struct NameCmp {
    using is_transparent = void;
    bool operator()(absl::string_view a, absl::string_view b) const {
      return a < b;
    }
  };

  // Defaults to -1.0 (unset).
  std::atomic<double> cpu_utilization_{-1.0};
  std::atomic<double> mem_utilization_{-1.0};
  std::atomic<double> qps_{-1.0};

  // To access GetMetrics().
  friend class grpc::BackendMetricState;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_BACKEND_METRIC_DATA_H
