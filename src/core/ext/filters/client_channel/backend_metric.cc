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
#include "src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.h"

namespace grpc_core {

namespace {

BackendMetricData* ParseBackendMetricData(
    const grpc_slice& serialized_load_report, Arena *arena) {
  upb::Arena upb_arena;
  upb_strview serialized_data = {
      reinterpret_cast<const char*>(
          GRPC_SLICE_START_PTR(serialized_load_report)),
      GRPC_SLICE_LENGTH(serialized_load_report)};
  udpa_data_orca_v1_OrcaLoadReport* msg =
      udpa_data_orca_v1_OrcaLoadReport_parsenew(serialized_data,
                                                upb_arena.ptr());
  if (msg == nullptr) return nullptr;
  BackendMetricData::MetricMap request_cost_or_utilization;
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
  return arena->New<BackendMetricData>(
      udpa_data_orca_v1_OrcaLoadReport_cpu_utilization(msg),
      udpa_data_orca_v1_OrcaLoadReport_mem_utilization(msg),
      udpa_data_orca_v1_OrcaLoadReport_rps(msg),
      std::move(request_cost_or_utilization));
}

}  // namespace

BackendMetricData* GetBackendMetricDataForCall(
    grpc_call_context_element* call_context,
    grpc_metadata_batch* recv_trailing_metadata, Arena *arena) {
  if (call_context[GRPC_CONTEXT_BACKEND_METRIC_DATA].value == nullptr) {
    grpc_linked_mdelem* md =
        recv_trailing_metadata->idx.named.x_endpoint_load_metrics_bin;
    if (md == nullptr) return nullptr;
    call_context[GRPC_CONTEXT_BACKEND_METRIC_DATA].value =
        ParseBackendMetricData(GRPC_MDVALUE(md->md), arena);
  }
  return static_cast<BackendMetricData*>(
      call_context[GRPC_CONTEXT_BACKEND_METRIC_DATA].value);
}

}  // namespace grpc_core
