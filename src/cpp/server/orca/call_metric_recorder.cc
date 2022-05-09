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

#include "absl/memory/memory.h"
#include "upb/upb.hpp"
#include "xds/data/orca/v3/orca_load_report.upb.h"

#include <grpcpp/impl/codegen/call_metric_recorder.h>

#include "src/core/ext/filters/common/backend_metric_data.h"

grpc::CallMetricRecorder::CallMetricRecorder()
    : backend_metric_data_(absl::make_unique<grpc_core::BackendMetricData>()) {}

grpc::CallMetricRecorder::~CallMetricRecorder() {}

grpc::CallMetricRecorder& grpc::CallMetricRecorder::RecordCpuUtilizationMetric(
    double value) {
  if (!disabled_) backend_metric_data_->cpu_utilization = value;
  return *this;
}
grpc::CallMetricRecorder&
grpc::CallMetricRecorder::RecordMemoryUtilizationMetric(double value) {
  if (!disabled_) backend_metric_data_->mem_utilization = value;
  return *this;
}
grpc::CallMetricRecorder& grpc::CallMetricRecorder::RecordRequestsPerSecond(
    uint32_t value) {
  if (!disabled_) backend_metric_data_->requests_per_second = value;
  return *this;
}
grpc::CallMetricRecorder& grpc::CallMetricRecorder::RecordUtilizationMetric(
    const absl::string_view& name, double value) {
  if (!disabled_) backend_metric_data_->utilization[name] = value;
  return *this;
}
grpc::CallMetricRecorder& grpc::CallMetricRecorder::RecordRequestCostMetric(
    const absl::string_view& name, double value) {
  if (!disabled_) backend_metric_data_->request_cost[name] = value;
  return *this;
}

std::string grpc::CallMetricRecorder::CreateSerializedReport() {
  upb::Arena arena;
  disabled_ = true;
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
  xds_data_orca_v3_OrcaLoadReport_set_rps(
      response, backend_metric_data_->requests_per_second);
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
