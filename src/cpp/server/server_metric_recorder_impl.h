/*
 *
 * Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CPP_SERVER_SERVER_METRIC_RECORDER_IMPL_H
#define GRPC_SRC_CPP_SERVER_SERVER_METRIC_RECORDER_IMPL_H

#include <functional>
#include <memory>
#include <map>

#include <grpcpp/ext/server_metric_recorder.h>
#include <grpcpp/impl/sync.h>
#include <grpcpp/support/string_ref.h>

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"

namespace grpc {
class BackendMetricState;
namespace experimental {

class ServerMetricRecorderImpl : public ServerMetricRecorder  {
 public:
  ServerMetricRecorderImpl();
  virtual ~ServerMetricRecorderImpl() = default;
  void SetCpuUtilization(double value) override;
  void SetMemoryUtilization(double value) override;
  void SetQps(double value) override;
  void SetNamedUtilization(string_ref name, double value) override;
  void SetAllNamedUtilization(std::map<string_ref, double> named_utilization) override;
  void ClearCpuUtilization() override;
  void ClearMemoryUtilization() override;
  void ClearQps() override;
  void ClearNamedUtilization(string_ref name) override;

 private:
  // To access GetMetrics().
  friend class grpc::BackendMetricState;
  friend class OrcaService;

  // Backend metrics and an associated update sequence number.
  struct BackendMetricDataState {
    grpc_core::BackendMetricData data;
    uint64_t sequence_number = 0;
  };

  // Updates the metric state by applying `updater` to the data and incrementing
  // the sequence number.
  void UpdateBackendMetricDataState(
      std::function<void(grpc_core::BackendMetricData*)> updater);

  grpc_core::BackendMetricData GetMetrics() const;
  // Returned metric data is guaranteed to be identical between two calls if the
  // sequence numbers match.
  std::shared_ptr<const BackendMetricDataState> GetMetricsIfChanged() const;

  mutable grpc::internal::Mutex mu_;
  std::shared_ptr<const BackendMetricDataState> metric_state_
      ABSL_GUARDED_BY(mu_);
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_SERVER_METRIC_RECORDER_IMPL_H
