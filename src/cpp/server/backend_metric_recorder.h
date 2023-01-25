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

#ifndef GRPC_SRC_CPP_SERVER_METRIC_RECORDER_H_
#define GRPC_SRC_CPP_SERVER_METRIC_RECORDER_H_

#include <map>

#include <grpcpp/ext/call_metric_recorder.h>

#include "src/core/ext/filters/backend_metrics/backend_metric_provider.h"

namespace grpc {

class BackendMetricState : public grpc_core::BackendMetricProvider,
                           public experimental::CallMetricRecorder {
 public:
  // `server_metric_recorder` is optional. When set, GetBackendMetricData()
  // merges metrics from `server_metric_recorder` with metrics recorded to this.
  explicit BackendMetricState(
      experimental::ServerMetricRecorder* server_metric_recorder)
      : server_metric_recorder_(server_metric_recorder) {}
  experimental::CallMetricRecorder& RecordCpuUtilizationMetric(
      double value) override;
  experimental::CallMetricRecorder& RecordMemoryUtilizationMetric(
      double value) override;
  experimental::CallMetricRecorder& RecordQpsMetric(double value) override;
  experimental::CallMetricRecorder& RecordUtilizationMetric(
      string_ref name, double value) override;
  experimental::CallMetricRecorder& RecordRequestCostMetric(
      string_ref name, double value) override;
  // This clears metrics currently recorded. Don't call twice.
  // Returns nullopt if data is empty.
  absl::optional<grpc_core::BackendMetricData> GetBackendMetricData() override;

 private:
  experimental::ServerMetricRecorder* server_metric_recorder_;
  std::atomic<double> cpu_utilization_{-1.0};
  std::atomic<double> mem_utilization_{-1.0};
  std::atomic<double> qps_{-1.0};
  internal::Mutex mu_;
  std::map<absl::string_view, double> utilization_ ABSL_GUARDED_BY(mu_);
  std::map<absl::string_view, double> request_cost_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_METRIC_RECORDER_H_
