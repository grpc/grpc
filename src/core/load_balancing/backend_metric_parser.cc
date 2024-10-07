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

#include "src/core/load_balancing/backend_metric_parser.h"

#include <grpc/support/port_platform.h>
#include <string.h>

#include <map>

#include "absl/strings/string_view.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"
#include "upb/message/map.h"
#include "xds/data/orca/v3/orca_load_report.upb.h"

namespace grpc_core {

namespace {

template <typename EntryType>
std::map<absl::string_view, double> ParseMap(
    xds_data_orca_v3_OrcaLoadReport* msg,
    const EntryType* (*entry_func)(const xds_data_orca_v3_OrcaLoadReport*,
                                   size_t*),
    upb_StringView (*key_func)(const EntryType*),
    double (*value_func)(const EntryType*),
    BackendMetricAllocatorInterface* allocator) {
  std::map<absl::string_view, double> result;
  size_t i = kUpb_Map_Begin;
  while (true) {
    const auto* entry = entry_func(msg, &i);
    if (entry == nullptr) break;
    upb_StringView key_view = key_func(entry);
    char* key = allocator->AllocateString(key_view.size);
    memcpy(key, key_view.data, key_view.size);
    result[absl::string_view(key, key_view.size)] = value_func(entry);
  }
  return result;
}

}  // namespace

const BackendMetricData* ParseBackendMetricData(
    absl::string_view serialized_load_report,
    BackendMetricAllocatorInterface* allocator) {
  upb::Arena upb_arena;
  xds_data_orca_v3_OrcaLoadReport* msg = xds_data_orca_v3_OrcaLoadReport_parse(
      serialized_load_report.data(), serialized_load_report.size(),
      upb_arena.ptr());
  if (msg == nullptr) return nullptr;
  BackendMetricData* backend_metric_data =
      allocator->AllocateBackendMetricData();
  backend_metric_data->cpu_utilization =
      xds_data_orca_v3_OrcaLoadReport_cpu_utilization(msg);
  backend_metric_data->mem_utilization =
      xds_data_orca_v3_OrcaLoadReport_mem_utilization(msg);
  backend_metric_data->application_utilization =
      xds_data_orca_v3_OrcaLoadReport_application_utilization(msg);
  backend_metric_data->qps =
      xds_data_orca_v3_OrcaLoadReport_rps_fractional(msg);
  backend_metric_data->eps = xds_data_orca_v3_OrcaLoadReport_eps(msg);
  backend_metric_data->request_cost =
      ParseMap<xds_data_orca_v3_OrcaLoadReport_RequestCostEntry>(
          msg, xds_data_orca_v3_OrcaLoadReport_request_cost_next,
          xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_key,
          xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_value, allocator);
  backend_metric_data->utilization =
      ParseMap<xds_data_orca_v3_OrcaLoadReport_UtilizationEntry>(
          msg, xds_data_orca_v3_OrcaLoadReport_utilization_next,
          xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_key,
          xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_value, allocator);
  backend_metric_data->named_metrics =
      ParseMap<xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry>(
          msg, xds_data_orca_v3_OrcaLoadReport_named_metrics_next,
          xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry_key,
          xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry_value, allocator);
  return backend_metric_data;
}

}  // namespace grpc_core
