//
// Copyright 2022 gRPC authors.
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

#include <stddef.h>

#include <map>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "upb/upb.h"
#include "upb/upb.hpp"
#include "xds/data/orca/v3/orca_load_report.upb.h"

#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/impl/codegen/sync.h>
#include <grpcpp/support/config.h>
#include <grpcpp/support/string_ref.h>

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc {
namespace experimental {

CallMetricRecorder::CallMetricRecorder(grpc_core::Arena* arena)
    : backend_metric_data_(arena->New<grpc_core::BackendMetricData>()) {}

CallMetricRecorder::~CallMetricRecorder() {
  backend_metric_data_->~BackendMetricData();
}

CallMetricRecorder& CallMetricRecorder::RecordCpuUtilizationMetric(
    double value) {
  internal::MutexLock lock(&mu_);
  backend_metric_data_->cpu_utilization = value;
  return *this;
}

CallMetricRecorder& CallMetricRecorder::RecordMemoryUtilizationMetric(
    double value) {
  internal::MutexLock lock(&mu_);
  backend_metric_data_->mem_utilization = value;
  return *this;
}

CallMetricRecorder& CallMetricRecorder::RecordUtilizationMetric(
    grpc::string_ref name, double value) {
  internal::MutexLock lock(&mu_);
  absl::string_view name_sv(name.data(), name.length());
  backend_metric_data_->utilization[name_sv] = value;
  return *this;
}

CallMetricRecorder& CallMetricRecorder::RecordRequestCostMetric(
    grpc::string_ref name, double value) {
  internal::MutexLock lock(&mu_);
  absl::string_view name_sv(name.data(), name.length());
  backend_metric_data_->request_cost[name_sv] = value;
  return *this;
}

absl::optional<std::string> CallMetricRecorder::CreateSerializedReport() {
  upb::Arena arena;
  internal::MutexLock lock(&mu_);
  bool has_data = backend_metric_data_->cpu_utilization != -1 ||
                  backend_metric_data_->mem_utilization != -1 ||
                  !backend_metric_data_->utilization.empty() ||
                  !backend_metric_data_->request_cost.empty();
  if (!has_data) {
    return absl::nullopt;
  }
  xds_data_orca_v3_OrcaLoadReport* response =
      xds_data_orca_v3_OrcaLoadReport_new(arena.ptr());
  if (backend_metric_data_->cpu_utilization != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_cpu_utilization(
        response, backend_metric_data_->cpu_utilization);
  }
  if (backend_metric_data_->mem_utilization != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_mem_utilization(
        response, backend_metric_data_->mem_utilization);
  }
  for (const auto& p : backend_metric_data_->request_cost) {
    xds_data_orca_v3_OrcaLoadReport_request_cost_set(
        response,
        upb_StringView_FromDataAndSize(p.first.data(), p.first.size()),
        p.second, arena.ptr());
  }
  for (const auto& p : backend_metric_data_->utilization) {
    xds_data_orca_v3_OrcaLoadReport_utilization_set(
        response,
        upb_StringView_FromDataAndSize(p.first.data(), p.first.size()),
        p.second, arena.ptr());
  }
  size_t buf_length;
  char* buf = xds_data_orca_v3_OrcaLoadReport_serialize(response, arena.ptr(),
                                                        &buf_length);
  return std::string(buf, buf_length);
}

}  // namespace experimental
}  // namespace grpc
