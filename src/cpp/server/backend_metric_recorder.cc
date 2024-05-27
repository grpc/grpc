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

#include "src/cpp/server/backend_metric_recorder.h"

#include <inttypes.h>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <grpc/support/log.h>
#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/ext/server_metric_recorder.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/load_balancing/backend_metric_data.h"

using grpc_core::BackendMetricData;

namespace {
// Utilization values with soft limits must be in [0, infy).
bool IsUtilizationWithSoftLimitsValid(double util) { return util >= 0.0; }

// Other utilization values must be in [0, 1].
bool IsUtilizationValid(double utilization) {
  return utilization >= 0.0 && utilization <= 1.0;
}

// Rate values (qps and eps) must be in [0, infy).
bool IsRateValid(double rate) { return rate >= 0.0; }

grpc_core::TraceFlag grpc_backend_metric_trace(false, "backend_metric");
}  // namespace

namespace grpc {
namespace experimental {

std::unique_ptr<ServerMetricRecorder> ServerMetricRecorder::Create() {
  return std::unique_ptr<ServerMetricRecorder>(new ServerMetricRecorder());
}

ServerMetricRecorder::ServerMetricRecorder()
    : metric_state_(std::make_shared<const BackendMetricDataState>()) {}

void ServerMetricRecorder::UpdateBackendMetricDataState(
    std::function<void(BackendMetricData*)> updater) {
  internal::MutexLock lock(&mu_);
  auto new_state = std::make_shared<BackendMetricDataState>(*metric_state_);
  updater(&new_state->data);
  ++new_state->sequence_number;
  metric_state_ = std::move(new_state);
}

void ServerMetricRecorder::SetCpuUtilization(double value) {
  if (!IsUtilizationWithSoftLimitsValid(value)) {
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

void ServerMetricRecorder::SetMemoryUtilization(double value) {
  if (!IsUtilizationValid(value)) {
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

void ServerMetricRecorder::SetApplicationUtilization(double value) {
  if (!IsUtilizationWithSoftLimitsValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Application utilization rejected: %f", this,
              value);
    }
    return;
  }
  UpdateBackendMetricDataState([value](BackendMetricData* data) {
    data->application_utilization = value;
  });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Application utilization set: %f", this, value);
  }
}

void ServerMetricRecorder::SetQps(double value) {
  if (!IsRateValid(value)) {
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

void ServerMetricRecorder::SetEps(double value) {
  if (!IsRateValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] EPS rejected: %f", this, value);
    }
    return;
  }
  UpdateBackendMetricDataState(
      [value](BackendMetricData* data) { data->eps = value; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] EPS set: %f", this, value);
  }
}

void ServerMetricRecorder::SetNamedUtilization(string_ref name, double value) {
  if (!IsUtilizationValid(value)) {
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

void ServerMetricRecorder::SetAllNamedUtilization(
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

void ServerMetricRecorder::ClearCpuUtilization() {
  UpdateBackendMetricDataState(
      [](BackendMetricData* data) { data->cpu_utilization = -1; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearMemoryUtilization() {
  UpdateBackendMetricDataState(
      [](BackendMetricData* data) { data->mem_utilization = -1; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearApplicationUtilization() {
  UpdateBackendMetricDataState(
      [](BackendMetricData* data) { data->application_utilization = -1; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Application utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearQps() {
  UpdateBackendMetricDataState([](BackendMetricData* data) { data->qps = -1; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearEps() {
  UpdateBackendMetricDataState([](BackendMetricData* data) { data->eps = -1; });
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] EPS utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearNamedUtilization(string_ref name) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Named utilization cleared. name: %s", this,
            std::string(name.data(), name.size()).c_str());
  }
  UpdateBackendMetricDataState([name](BackendMetricData* data) {
    data->utilization.erase(absl::string_view(name.data(), name.size()));
  });
}

grpc_core::BackendMetricData ServerMetricRecorder::GetMetrics() const {
  auto result = GetMetricsIfChanged();
  return result->data;
}

std::shared_ptr<const ServerMetricRecorder::BackendMetricDataState>
ServerMetricRecorder::GetMetricsIfChanged() const {
  std::shared_ptr<const BackendMetricDataState> result;
  {
    internal::MutexLock lock(&mu_);
    result = metric_state_;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    const auto& data = result->data;
    gpr_log(GPR_INFO,
            "[%p] GetMetrics() returned: seq:%" PRIu64
            " cpu:%f mem:%f app:%f qps:%f eps:%f utilization size: %" PRIuPTR,
            this, result->sequence_number, data.cpu_utilization,
            data.mem_utilization, data.application_utilization, data.qps,
            data.eps, data.utilization.size());
  }
  return result;
}

}  // namespace experimental

experimental::CallMetricRecorder&
BackendMetricState::RecordCpuUtilizationMetric(double value) {
  if (!IsUtilizationWithSoftLimitsValid(value)) {
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

experimental::CallMetricRecorder&
BackendMetricState::RecordApplicationUtilizationMetric(double value) {
  if (!IsUtilizationWithSoftLimitsValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Application utilization value rejected: %f", this,
              value);
    }
    return *this;
  }
  application_utilization_.store(value, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Application utilization recorded: %f", this, value);
  }
  return *this;
}

experimental::CallMetricRecorder& BackendMetricState::RecordQpsMetric(
    double value) {
  if (!IsRateValid(value)) {
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

experimental::CallMetricRecorder& BackendMetricState::RecordEpsMetric(
    double value) {
  if (!IsRateValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] EPS value rejected: %f", this, value);
    }
    return *this;
  }
  eps_.store(value, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] EPS recorded: %f", this, value);
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

experimental::CallMetricRecorder& BackendMetricState::RecordNamedMetric(
    string_ref name, double value) {
  internal::MutexLock lock(&mu_);
  absl::string_view name_sv(name.data(), name.length());
  named_metrics_[name_sv] = value;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Named metric recorded: %s %f", this,
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
  if (IsUtilizationWithSoftLimitsValid(cpu)) {
    data.cpu_utilization = cpu;
  }
  const double mem = mem_utilization_.load(std::memory_order_relaxed);
  if (IsUtilizationValid(mem)) {
    data.mem_utilization = mem;
  }
  const double app_util =
      application_utilization_.load(std::memory_order_relaxed);
  if (IsUtilizationWithSoftLimitsValid(app_util)) {
    data.application_utilization = app_util;
  }
  const double qps = qps_.load(std::memory_order_relaxed);
  if (IsRateValid(qps)) {
    data.qps = qps;
  }
  const double eps = eps_.load(std::memory_order_relaxed);
  if (IsRateValid(eps)) {
    data.eps = eps;
  }
  {
    internal::MutexLock lock(&mu_);
    for (const auto& u : utilization_) {
      data.utilization[u.first] = u.second;
    }
    for (const auto& r : request_cost_) {
      data.request_cost[r.first] = r.second;
    }
    for (const auto& r : named_metrics_) {
      data.named_metrics[r.first] = r.second;
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO,
            "[%p] Backend metric data returned: cpu:%f mem:%f qps:%f eps:%f "
            "utilization size:%" PRIuPTR " request_cost size:%" PRIuPTR
            "named_metrics size:%" PRIuPTR,
            this, data.cpu_utilization, data.mem_utilization, data.qps,
            data.eps, data.utilization.size(), data.request_cost.size(),
            data.named_metrics.size());
  }
  return data;
}

}  // namespace grpc
