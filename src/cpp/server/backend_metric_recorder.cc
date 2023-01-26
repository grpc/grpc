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

#include "src/core/lib/debug/trace.h"
#include "src/cpp/server/backend_metric_recorder.h"

using grpc_core::BackendMetricData;

namespace {
// All utilization values must be in [0, 1].
bool IsUtilizationValid(double utilization) {
  return utilization >= 0.0 && utilization <= 1.0;
}

// QPS must be in [0, infy).
bool IsQpsValid(double qps) { return qps >= 0.0; }

bool IsEmpty(const BackendMetricData& data) {
  return !IsUtilizationValid(data.cpu_utilization) &&
         !IsUtilizationValid(data.mem_utilization) && !IsQpsValid(data.qps) &&
         data.request_cost.empty() && data.utilization.empty();
}

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
  internal::MutexLock lock(&mu_);
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization set: %f seq: %lu", this, value,
            old_seq + 1);
  }
  cpu_utilization_ = value;
}

void ServerMetricRecorder::SetMemoryUtilization(double value) {
  if (!IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] Mem utilization rejected: %f", this, value);
    }
    return;
  }
  internal::MutexLock lock(&mu_);
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization set: %f seq: %lu", this, value,
            old_seq + 1);
  }
  mem_utilization_ = value;
}

void ServerMetricRecorder::SetQps(double value) {
  if (!IsQpsValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] QPS rejected: %f", this, value);
    }
    return;
  }
  internal::MutexLock lock(&mu_);
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS set: %f seq: %lu", this, value, old_seq + 1);
  }
  qps_ = value;
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
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Named utilization set: %f name: %s seq: %lu", this,
            value, name.c_str(), old_seq + 1);
  }
  named_utilization_[std::move(name)] = value;
}

void ServerMetricRecorder::SetAllNamedUtilization(
    std::map<std::string, double> named_utilization) {
  internal::MutexLock lock(&mu_);
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO,
            "[%p] All named utilization updated. size: %" PRIuPTR " seq: %lu",
            this, named_utilization.size(), old_seq + 1);
  }
  named_utilization_ = std::move(named_utilization);
}

void ServerMetricRecorder::ClearCpuUtilization() {
  internal::MutexLock lock(&mu_);
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization cleared. seq: %lu", this,
            old_seq + 1);
  }
  cpu_utilization_ = -1.0;
}

void ServerMetricRecorder::ClearMemoryUtilization() {
  internal::MutexLock lock(&mu_);
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization cleared. seq: %lu", this,
            old_seq + 1);
  }
  mem_utilization_ = -1.0;
}

void ServerMetricRecorder::ClearQps() {
  internal::MutexLock lock(&mu_);
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS utilization cleared. seq: %lu", this,
            old_seq + 1);
  }
  qps_ = -1.0;
}

void ServerMetricRecorder::ClearNamedUtilization(absl::string_view name) {
  std::string name_str(name);
  internal::MutexLock lock(&mu_);
  uint64_t old_seq = seq_.fetch_add(1, std::memory_order_acq_rel);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(GPR_INFO, "[%p] Named utilization cleared. name: %s seq: %lu", this,
            name_str.c_str(), old_seq + 1);
  }
  named_utilization_.erase(name_str);
}

std::pair<absl::optional<BackendMetricData>, uint64_t>
ServerMetricRecorder::GetMetrics(absl::optional<uint64_t> last_seq) const {
  uint64_t seq = seq_.load(std::memory_order_acquire);
  if (last_seq.has_value() && *last_seq == seq) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
      gpr_log(GPR_INFO, "[%p] GetMetrics() returned with no change: seq:%lu",
              this, seq);
    }
    return std::make_pair(absl::nullopt, seq);
  }
  BackendMetricData data;
  internal::MutexLock lock(&mu_);
  if (IsUtilizationValid(cpu_utilization_)) {
    data.cpu_utilization = cpu_utilization_;
  }
  if (IsUtilizationValid(mem_utilization_)) {
    data.mem_utilization = mem_utilization_;
  }
  if (IsQpsValid(qps_)) {
    data.qps = qps_;
  }
  for (const auto& nu : named_utilization_) {
    data.utilization[nu.first] = nu.second;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_trace)) {
    gpr_log(
        GPR_INFO,
        "[%p] GetMetrics() returned: seq:%lu cpu:%f mem:%f qps:%f utilization "
        "size: %" PRIuPTR,
        this, seq, data.cpu_utilization, data.mem_utilization, data.qps,
        data.utilization.size());
  }
  seq = seq_.load(std::memory_order_relaxed);
  if (IsEmpty(data)) return std::make_pair(absl::nullopt, seq);
  return std::make_pair(std::move(data), seq);
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

absl::optional<BackendMetricData> BackendMetricState::GetBackendMetricData() {
  // Merge metrics from the ServerMetricRecorder first since metrics recorded
  // to CallMetricRecorder takes a higher precedence.
  BackendMetricData data;
  if (server_metric_recorder_ != nullptr) {
    absl::optional<BackendMetricData> server_data =
        server_metric_recorder_->GetMetrics().first;
    if (server_data.has_value()) {
      data = std::move(*server_data);
    }
  }
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
  if (IsEmpty(data)) return absl::nullopt;
  return data;
}

}  // namespace grpc
