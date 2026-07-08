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
#include <type_traits>

#include "src/core/load_balancing/backend_metric_data.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"
#include "upb/message/map.h"
#include "xds/data/orca/v3/orca_load_report.upb.h"
#include "absl/strings/string_view.h"

// Forward-declare the new typed map Entry structures to satisfy compiler types.
struct xds_data_orca_v3_OrcaLoadReport_RequestCostEntry;
struct xds_data_orca_v3_OrcaLoadReport_UtilizationEntry;
struct xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry;

extern "C" {
// Declare the new getter functions strongly. They are only referenced
// if the new upb is active, via template specialization.
upb_StringView xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_key(
    const xds_data_orca_v3_OrcaLoadReport_RequestCostEntry*);
double xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_value(
    const xds_data_orca_v3_OrcaLoadReport_RequestCostEntry*);

upb_StringView xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_key(
    const xds_data_orca_v3_OrcaLoadReport_UtilizationEntry*);
double xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_value(
    const xds_data_orca_v3_OrcaLoadReport_UtilizationEntry*);

upb_StringView xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry_key(
    const xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry*);
double xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry_value(
    const xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry*);
}

namespace grpc_core {

// Template declarations for entry getters.
template <typename Entry>
upb_StringView GetMapKey(const Entry* entry);

template <typename Entry>
double GetMapVal(const Entry* entry);

// Specializations for RequestCostEntry.
template <>
inline upb_StringView GetMapKey(
    const xds_data_orca_v3_OrcaLoadReport_RequestCostEntry* entry) {
  return xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_key(entry);
}
template <>
inline double GetMapVal(
    const xds_data_orca_v3_OrcaLoadReport_RequestCostEntry* entry) {
  return xds_data_orca_v3_OrcaLoadReport_RequestCostEntry_value(entry);
}

// Specializations for UtilizationEntry.
template <>
inline upb_StringView GetMapKey(
    const xds_data_orca_v3_OrcaLoadReport_UtilizationEntry* entry) {
  return xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_key(entry);
}
template <>
inline double GetMapVal(
    const xds_data_orca_v3_OrcaLoadReport_UtilizationEntry* entry) {
  return xds_data_orca_v3_OrcaLoadReport_UtilizationEntry_value(entry);
}

// Specializations for NamedMetricsEntry.
template <>
inline upb_StringView GetMapKey(
    const xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry* entry) {
  return xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry_key(entry);
}
template <>
inline double GetMapVal(
    const xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry* entry) {
  return xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry_value(entry);
}

namespace {

// Traits helper to reliably detect if a next function is legacy (returns bool)
template <typename T>
struct is_legacy_next_func : std::false_type {};

template <typename... Args>
struct is_legacy_next_func<bool (*)(Args...)> : std::true_type {};

template <typename... Args>
struct is_legacy_next_func<bool(Args...)> : std::true_type {};

template <typename NextFunc>
decltype(BackendMetricData::request_cost) ParseMap(
    xds_data_orca_v3_OrcaLoadReport* msg, NextFunc upb_next_func,
    BackendMetricAllocatorInterface* allocator) {
  decltype(BackendMetricData::request_cost) result;
  size_t i = kUpb_Map_Begin;

  // Check return-type using our traits helper
  if constexpr (is_legacy_next_func<NextFunc>::value) {
    // Legacy direct-map-unpacking path (compiled if old upb is active)
    upb_StringView key_view;
    double value;
    while (upb_next_func(msg, &key_view, &value, &i)) {
      char* key = allocator->AllocateString(key_view.size);
      memcpy(key, key_view.data, key_view.size);
      result[absl::string_view(key, key_view.size)] = value;
    }
  } else {
    // Modern Entry-unpacking path (compiled if new upb is active)
    while (true) {
      auto entry = upb_next_func(msg, &i);
      if (entry == nullptr) {
        break;
      }
      // Resolves key using template helper
      upb_StringView key_view = GetMapKey(entry);
      // Resolves value using template helper
      double value = GetMapVal(entry);
      char* key = allocator->AllocateString(key_view.size);
      memcpy(key, key_view.data, key_view.size);
      result[absl::string_view(key, key_view.size)] = value;
    }
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
  backend_metric_data->request_cost = ParseMap(
      msg, xds_data_orca_v3_OrcaLoadReport_request_cost_next, allocator);
  backend_metric_data->utilization = ParseMap(
      msg, xds_data_orca_v3_OrcaLoadReport_utilization_next, allocator);
  backend_metric_data->named_metrics = ParseMap(
      msg, xds_data_orca_v3_OrcaLoadReport_named_metrics_next, allocator);
  return backend_metric_data;
}

}  // namespace grpc_core
