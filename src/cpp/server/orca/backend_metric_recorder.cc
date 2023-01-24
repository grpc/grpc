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
#include <grpcpp/ext/server_metric_recorder.h>

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/lib/debug/trace.h"
#include "src/cpp/server/backend_metric_recorder.h"

namespace {
// All utilization values must be in [0, 1].
bool IsUtilizationValid(double utilization) {
  return utilization >= 0.0 && utilization <= 1.0;
}

// QPS must be in [0, infy).
bool IsQpsValid(double qps) { return qps >= 0.0; }

grpc_core::TraceFlag grpc_backend_metric_trace(false, "backend_metric");
}  // namespace

namespace grpc {
namespace experimental {

void ServerMetricRecorder::SetCpuUtilization(double value) {
  if (!IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] CPU utilization rejected: %f", this, value);
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization set: %f", this, value);
  }
  update_seq_.fetch_add(1, std::memory_order_acq_rel);
  cpu_utilization_.store(value, std::memory_order_relaxed);
}

void ServerMetricRecorder::SetMemoryUtilization(double value) {
  if (!IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Mem utilization rejected: %f", this, value);
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization set: %f", this, value);
  }
  update_seq_.fetch_add(1, std::memory_order_acq_rel);
  mem_utilization_.store(value, std::memory_order_relaxed);
}

void ServerMetricRecorder::SetQps(double value) {
  if (!IsQpsValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] QPS rejected: %f", this, value);
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS set: %f", this, value);
  }
  update_seq_.fetch_add(1, std::memory_order_acq_rel);
  qps_.store(value, std::memory_order_relaxed);
}

void ServerMetricRecorder::SetNamedUtilization(std::string name, double value) {
  if (!IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Named utilization rejected: %f name: %s", this,
              value, name.c_str());
    }
    return;
  }
  internal::MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Named utilization set: %f name: %s", this, value,
            name.c_str());
  }
  update_seq_.fetch_add(1, std::memory_order_relaxed);
  named_utilization_[std::move(name)] = value;
}

void ServerMetricRecorder::SetAllNamedUtilization(
    std::map<std::string, double> named_utilization) {
  internal::MutexLock lock(&mu_);
  update_seq_.fetch_add(1, std::memory_order_relaxed);
  named_utilization_ = std::move(named_utilization);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] All named utilization updated.", this);
  }
}

void ServerMetricRecorder::ClearCpuUtilization() {
  update_seq_.fetch_add(1, std::memory_order_acq_rel);
  cpu_utilization_.store(-1.0, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearMemoryUtilization() {
  update_seq_.fetch_add(1, std::memory_order_acq_rel);
  mem_utilization_.store(-1.0, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearQps() {
  update_seq_.fetch_add(1, std::memory_order_acq_rel);
  qps_.store(-1.0, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearNamedUtilization(absl::string_view name) {
  std::string name_str(name);
  internal::MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Named utilization cleared. name: %s", this,
            name_str.c_str());
  }
  update_seq_.fetch_add(1, std::memory_order_relaxed);
  named_utilization_.erase(name_str);
}

std::pair<grpc_core::BackendMetricData, uint64_t>
ServerMetricRecorder::GetMetrics() const {
  grpc_core::BackendMetricData data;
  const double cpu = cpu_utilization_.load(std::memory_order_relaxed);
  if (IsUtilizationValid(cpu)) {
    data.cpu_utilization = cpu;
  }
  const double mem = mem_utilization_.load(std::memory_order_relaxed);
  if (IsUtilizationValid(mem)) {
    data.mem_utilization = mem;
  }
  const double qps = qps_.load(std::memory_order_relaxed);
  if (IsQpsValid(qps)) {
    data.qps = qps;
  }
  {
    internal::MutexLock lock(&mu_);
    for (const auto& nu : named_utilization_) {
      data.utilization[nu.first] = nu.second;
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO,
            "[%p] GetMetrics() returned: cpu:%f mem:%f qps:%f utilization "
            "size: %" PRIuPTR,
            this, data.cpu_utilization, data.mem_utilization, data.qps,
            data.utilization.size());
  }
  return std::make_pair<grpc_core::BackendMetricData, uint64_t>(
      std::move(data), update_seq_.load(std::memory_order_acquire));
}

}  // namespace experimental

experimental::CallMetricRecorder&
BackendMetricState::RecordCpuUtilizationMetric(double value) {
  if (!IsUtilizationValid(value)) {
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
  if (!IsUtilizationValid(value)) {
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
  if (!IsQpsValid(value)) {
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
  if (!IsUtilizationValid(value)) {
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

grpc_core::BackendMetricData BackendMetricState::GetBackendMetricData()
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // Merge metrics from the ServerMetricRecorder first since metrics recorded
  // to CallMetricRecorder takes a higher precedence.
  grpc_core::BackendMetricData data =
      server_metric_recorder_ == nullptr
          ? grpc_core::BackendMetricData()
          : server_metric_recorder_->GetMetrics().first;
  // Only overwrite if the value is set i.e. in the valid range.
  const double cpu = cpu_utilization_.load(std::memory_order_relaxed);
  if (IsUtilizationValid(cpu)) {
    data.cpu_utilization = cpu;
  }
  const double mem = mem_utilization_.load(std::memory_order_relaxed);
  if (IsUtilizationValid(mem)) {
    data.mem_utilization = mem;
  }
  const double qps = qps_.load(std::memory_order_relaxed);
  if (IsQpsValid(qps)) {
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
