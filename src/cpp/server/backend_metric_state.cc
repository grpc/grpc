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

#include <grpc/support/log.h>
#include <grpcpp/ext/call_metric_recorder.h>

#include "src/core/lib/debug/trace.h"
#include "src/cpp/server/backend_metric_state.h"

using grpc_core::BackendMetricData;
grpc_core::TraceFlag grpc_backend_metric_trace(false, "backend_metric");

namespace grpc {

experimental::CallMetricRecorder&
BackendMetricState::RecordCpuUtilizationMetric(double value) {
  if (!BackendMetricData::IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] CPU utilization value rejected: %f", this, value);
    }
    return *this;
  }
  cpu_utilization_.store(value, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization recorded: %f", this, value);
  }
  return *this;
}

experimental::CallMetricRecorder&
BackendMetricState::RecordMemoryUtilizationMetric(double value) {
  if (!BackendMetricData::IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Mem utilization value rejected: %f", this, value);
    }
    return *this;
  }
  mem_utilization_.store(value, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization recorded: %f", this, value);
  }
  return *this;
}

experimental::CallMetricRecorder& BackendMetricState::RecordQpsMetric(
    double value) {
  if (!BackendMetricData::IsQpsValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] QPS value rejected: %f", this, value);
    }
    return *this;
  }
  qps_.store(value, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS recorded: %f", this, value);
  }
  return *this;
}

experimental::CallMetricRecorder& BackendMetricState::RecordUtilizationMetric(
    string_ref name, double value) {
  if (!BackendMetricData::IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Utilization value rejected: %s %f", this,
              std::string(name.data(), name.length()).c_str(), value);
    }
    return *this;
  }
  internal::MutexLock lock(&mu_);
  absl::string_view name_sv(name.data(), name.length());
  utilization_[name_sv] = value;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Utilization recorded: %s %f", this,
            std::string(name_sv).c_str(), value);
  }
  return *this;
}

experimental::CallMetricRecorder& BackendMetricState::RecordRequestCostMetric(
    string_ref name, double value) {
  internal::MutexLock lock(&mu_);
  absl::string_view name_sv(name.data(), name.length());
  request_cost_[name_sv] = value;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Request cost recorded: %s %f", this,
            std::string(name_sv).c_str(), value);
  }
  return *this;
}

BackendMetricData BackendMetricState::GetBackendMetricData() {
  // Merge metrics from the ServerMetricRecorder first since metrics recorded
  // to CallMetricRecorder takes a higher precedence.
  BackendMetricData data;
  if (server_metric_recorder_ != nullptr) {
    data = server_metric_recorder_->GetMetrics();
  }
  // Only overwrite if the value is set i.e. in the valid range.
  const double cpu = cpu_utilization_.load(std::memory_order_relaxed);
  if (BackendMetricData::IsUtilizationValid(cpu)) {
    data.cpu_utilization = cpu;
  }
  const double mem = mem_utilization_.load(std::memory_order_relaxed);
  if (BackendMetricData::IsUtilizationValid(mem)) {
    data.mem_utilization = mem;
  }
  const double qps = qps_.load(std::memory_order_relaxed);
  if (BackendMetricData::IsQpsValid(qps)) {
    data.qps = qps;
  }
  {
    internal::MutexLock lock(&mu_);
    data.utilization = std::move(utilization_);
    data.request_cost = std::move(request_cost_);
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO,
            "[%p] Backend metric data returned: cpu:%f mem:%f qps:%f "
            "utilization size:%" PRIuPTR " request_cost size:%" PRIuPTR,
            this, data.cpu_utilization, data.mem_utilization, data.qps,
            data.utilization.size(), data.request_cost.size());
  }
  return data;
}

}  // namespace grpc
