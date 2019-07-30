//
// Copyright 2019 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/backend_metric.h"
#include "udpa/data/orca/v1/orca_load_report.upb.h"

namespace grpc_core {

const LoadBalancingPolicy::BackendMetricData* ParseBackendMetricData(
    const grpc_slice& serialized_load_report, Arena* arena) {
  upb::Arena upb_arena;
  udpa_data_orca_v1_OrcaLoadReport* msg =
      udpa_data_orca_v1_OrcaLoadReport_parse(
          reinterpret_cast<const char*>(
              GRPC_SLICE_START_PTR(serialized_load_report)),
          GRPC_SLICE_LENGTH(serialized_load_report), upb_arena.ptr());
  if (msg == nullptr) return nullptr;
  Map<const char*, double, StringLess> request_cost_or_utilization;
  size_t size;
  const udpa_data_orca_v1_OrcaLoadReport_RequestCostOrUtilizationEntry* const*
      entries =
          udpa_data_orca_v1_OrcaLoadReport_mutable_request_cost_or_utilization(
              msg, &size);
  for (size_t i = 0; i < size; ++i) {
    upb_strview key_view =
        udpa_data_orca_v1_OrcaLoadReport_RequestCostOrUtilizationEntry_key(
            entries[i]);
    char* key = static_cast<char*>(arena->Alloc(key_view.size + 1));
    memcpy(key, key_view.data, key_view.size);
    key[key_view.size] = '\0';
    request_cost_or_utilization[key] =
        udpa_data_orca_v1_OrcaLoadReport_RequestCostOrUtilizationEntry_value(
            entries[i]);
  }
  LoadBalancingPolicy::BackendMetricData* backend_metric_data =
      arena->New<LoadBalancingPolicy::BackendMetricData>();
  backend_metric_data->cpu_utilization =
      udpa_data_orca_v1_OrcaLoadReport_cpu_utilization(msg);
  backend_metric_data->mem_utilization =
      udpa_data_orca_v1_OrcaLoadReport_mem_utilization(msg);
  backend_metric_data->rps = udpa_data_orca_v1_OrcaLoadReport_rps(msg);
  backend_metric_data->request_cost_or_utilization =
      std::move(request_cost_or_utilization);
  return backend_metric_data;
}

}  // namespace grpc_core
