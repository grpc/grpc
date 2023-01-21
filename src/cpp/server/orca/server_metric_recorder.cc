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

#include <grpc/support/log.h>
#include <grpcpp/ext/server_metric_recorder.h>

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/lib/debug/trace.h"

namespace grpc {
namespace experimental {

grpc_core::TraceFlag grpc_server_metric_recorder_trace(
    false, "server_metric_recorder");

namespace {
// All utilization values must be in [0, 1].
bool IsUtilizationValid(double utilization) {
  return utilization >= 0.0 && utilization <= 1.0;
}

// QPS must be in [0, infy).
bool IsQpsValid(double qps) { return qps >= 0.0; }

}  // namespace

void ServerMetricRecorder::SetCpuUtilization(double value) {
  if (!IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
      gpr_log(GPR_INFO, "[%p] CPU utilization rejected: %f", this, value);
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization set: %f", this, value);
  }
  cpu_utilization_.store(value, std::memory_order_relaxed);
}

void ServerMetricRecorder::SetMemoryUtilization(double value) {
  if (!IsUtilizationValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
      gpr_log(GPR_INFO, "[%p] Mem utilization rejected: %f", this, value);
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization set: %f", this, value);
  }
  mem_utilization_.store(value, std::memory_order_relaxed);
}

void ServerMetricRecorder::SetQps(double value) {
  if (!IsQpsValid(value)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
      gpr_log(GPR_INFO, "[%p] QPS rejected: %f", this, value);
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS set: %f", this, value);
  }
  qps_.store(value, std::memory_order_relaxed);
}

void ServerMetricRecorder::ClearCpuUtilization() {
  cpu_utilization_.store(-1.0, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
    gpr_log(GPR_INFO, "[%p] CPU utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearMemoryUtilization() {
  mem_utilization_.store(-1.0, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
    gpr_log(GPR_INFO, "[%p] Mem utilization cleared.", this);
  }
}

void ServerMetricRecorder::ClearQps() {
  qps_.store(-1.0, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
    gpr_log(GPR_INFO, "[%p] QPS utilization cleared.", this);
  }
}

grpc_core::BackendMetricData ServerMetricRecorder::GetMetrics() const {
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_server_metric_recorder_trace)) {
    gpr_log(GPR_INFO, "[%p] GetMetrics() returned: cpu:%f mem:%f qps:%f", this,
            data.cpu_utilization, data.mem_utilization, data.qps);
  }
  return data;
}

}  // namespace experimental
}  // namespace grpc
