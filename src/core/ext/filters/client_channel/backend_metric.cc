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

#include "absl/strings/string_view.h"
#include "upb/upb.hpp"
#include "xds/data/orca/v3/orca_load_report.upb.h"

namespace grpc_core {

namespace {

template <typename EntryType>
std::map<absl::string_view, double> ParseMap(
    xds_data_orca_v3_OrcaLoadReport* msg,
    const EntryType* (*entry_func)(const xds_data_orca_v3_OrcaLoadReport*,
                                   size_t*),
    upb_StringView (*key_func)(const EntryType*),
    double (*value_func)(const EntryType*), Arena* arena) {
  std::map<absl::string_view, double> result;
  size_t i = kUpb_Map_Begin;
  while (true) {
    const auto* entry = entry_func(msg, &i);
    if (entry == nullptr) break;
    upb_StringView key_view = key_func(entry);
    char* key = static_cast<char*>(arena->Alloc(key_view.size));
    memcpy(key, key_view.data, key_view.size);
    result[absl::string_view(key, key_view.size)] = value_func(entry);
  }
  return result;
}

}  // namespace

bool ParseBackendMetricData(
    absl::string_view serialized_load_report,
    LoadBalancingPolicy::BackendMetricAccessor::BackendMetricData* out) {
  upb::Arena upb_arena;
  xds_data_orca_v3_OrcaLoadReport* msg = xds_data_orca_v3_OrcaLoadReport_parse(
      serialized_load_report.data(), serialized_load_report.size(),
      upb_arena.ptr());
  if (msg == nullptr) return false;
  out->cpu_utilization =
      xds_data_orca_v3_OrcaLoadReport_cpu_utilization(msg);
  out->mem_utilization =
      xds_data_orca_v3_OrcaLoadReport_mem_utilization(msg);
  out->requests_per_second =
      xds_data_orca_v3_OrcaLoadReport_rps(msg);
  out->request_cost =
      ParseMap<xds_data_orca_v3_OrcaLoadReport_RequestCostEntry>(
          msg, xds_data_orca_v3_OrcaLoadReport_request_cost_next,
          xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_key,
          xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_value, arena);
  out->utilization =
      ParseMap<xds_data_orca_v3_OrcaLoadReport_UtilizationEntry>(
          msg, xds_data_orca_v3_OrcaLoadReport_utilization_next,
          xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_key,
          xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_value, arena);
  return true;
}

}  // namespace grpc_core
