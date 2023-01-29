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

#include <inttypes.h>

#include "src/core/lib/debug/trace.h"
#include "src/cpp/server/server_metric_recorder_impl.h"

using grpc_core::BackendMetricData;
grpc_core::TraceFlag grpc_backend_metric_trace(false, "backend_metric");

namespace grpc {
namespace experimental {

std::unique_ptr<ServerMetricRecorder> CreateServerMetricRecorder() {
  return std::make_unique<ServerMetricRecorderImpl>();
}

ServerMetricRecorderImpl::ServerMetricRecorderImpl() {
  // Starts with an empty result.
  metric_state_ = std::make_shared<const BackendMetricDataState>();
}

void ServerMetricRecorderImpl::UpdateBackendMetricDataState(
    std::function<void(BackendMetricData*)> updater) {
  internal::MutexLock lock(&mu_);
  auto new_state = std::make_shared<BackendMetricDataState>(*metric_state_);
  updater(&(new_state->data));
  ++new_state->sequence_number;
  metric_state_ = std::move(new_state);
}

void ServerMetricRecorderImpl::SetCpuUtilization(double value) {
  if (!BackendMetricData::IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] CPU utilization rejected: %f", this, value);
    }
    return;
  }
  UpdateBackendMetricDataState(
      [value](BackendMetricData* data) { data->cpu_utilization = value; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization set: %f", this, value);
  }
}

void ServerMetricRecorderImpl::SetMemoryUtilization(double value) {
  if (!BackendMetricData::IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Mem utilization rejected: %f", this, value);
    }
    return;
  }
  UpdateBackendMetricDataState(
      [value](BackendMetricData* data) { data->mem_utilization = value; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization set: %f", this, value);
  }
}

void ServerMetricRecorderImpl::SetQps(double value) {
  if (!BackendMetricData::IsQpsValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] QPS rejected: %f", this, value);
    }
    return;
  }
  UpdateBackendMetricDataState(
      [value](BackendMetricData* data) { data->qps = value; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS set: %f", this, value);
  }
}

void ServerMetricRecorderImpl::SetNamedUtilization(string_ref name, double value) {
  if (!BackendMetricData::IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Named utilization rejected: %f name: %s", this,
              value, std::string(name.data(), name.size()).c_str());
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Named utilization set: %f name: %s", this, value,
            std::string(name.data(), name.size()).c_str());
  }
  UpdateBackendMetricDataState([name, value](BackendMetricData* data) {
    data->utilization[absl::string_view(name.data(), name.size())] = value;
  });
}

void ServerMetricRecorderImpl::SetAllNamedUtilization(
    std::map<string_ref, double> named_utilization) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] All named utilization updated. size: %" PRIuPTR,
            this, named_utilization.size());
  }
  UpdateBackendMetricDataState(
      [utilization = std::move(named_utilization)](BackendMetricData* data) {
        data->utilization.clear();
        for (const auto& u : utilization) {
          data->utilization[absl::string_view(u.first.data(), u.first.size())] =
              u.second;
        }
      });
}

void ServerMetricRecorderImpl::ClearCpuUtilization() {
  UpdateBackendMetricDataState(
      [](BackendMetricData* data) { data->cpu_utilization = -1; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization cleared.", this);
  }
}

void ServerMetricRecorderImpl::ClearMemoryUtilization() {
  UpdateBackendMetricDataState(
      [](BackendMetricData* data) { data->mem_utilization = -1; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization cleared.", this);
  }
}

void ServerMetricRecorderImpl::ClearQps() {
  UpdateBackendMetricDataState([](BackendMetricData* data) { data->qps = -1; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS utilization cleared.", this);
  }
}

void ServerMetricRecorderImpl::ClearNamedUtilization(string_ref name) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Named utilization cleared. name: %s", this,
            std::string(name.data(), name.size()).c_str());
  }
  UpdateBackendMetricDataState([name](BackendMetricData* data) {
    data->utilization.erase(absl::string_view(name.data(), name.size()));
  });
}

grpc_core::BackendMetricData ServerMetricRecorderImpl::GetMetrics() const {
  auto result = GetMetricsIfChanged();
  return result->data;
}

std::shared_ptr<const ServerMetricRecorderImpl::BackendMetricDataState>
ServerMetricRecorderImpl::GetMetricsIfChanged() const {
  std::shared_ptr<const BackendMetricDataState> result;
  {
    internal::MutexLock lock(&mu_);
    result = metric_state_;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    const auto& data = result->data;
    gpr_log(
        GPR_INFO,
        "[%p] GetMetrics() returned: seq:%lu cpu:%f mem:%f qps:%f utilization "
        "size: %" PRIuPTR,
        this, result->sequence_number, data.cpu_utilization,
        data.mem_utilization, data.qps, data.utilization.size());
  }
  return result;
}

}  // namespace experimental
}  // namespace grpc
