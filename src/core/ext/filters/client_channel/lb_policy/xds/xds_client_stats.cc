/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_client_stats.h"

#include <grpc/support/atm.h>
#include <grpc/support/string_util.h>
#include <string.h>

namespace grpc_core {

namespace {

gpr_atm AtomicGetAndResetCounter(gpr_atm* counter) {
  return gpr_atm_full_xchg(counter, static_cast<gpr_atm>(0));
}

}  // namespace

//
// XdsLbClientStats::LocalityStats::LoadMetric
//

XdsLbClientStats::LocalityStats::LoadMetric
XdsLbClientStats::LocalityStats::LoadMetric::Harvest() {
  LoadMetric metric;
  metric.metric_name.reset(gpr_strdup(metric_name.get()));
  metric.num_requests_finished_with_metric =
      AtomicGetAndResetCounter(&num_requests_finished_with_metric);
  metric.total_metric_value = AtomicGetAndResetCounter(&total_metric_value);
  return metric;
}

//
// XdsLbClientStats::LocalityStats
//

XdsLbClientStats::LocalityStats XdsLbClientStats::LocalityStats::Harvest() {
  LocalityStats stats;
  stats.total_successful_requests =
      AtomicGetAndResetCounter(&total_successful_requests);
  stats.total_requests_in_progress =
      AtomicGetAndResetCounter(&total_requests_in_progress);
  stats.total_error_requests = AtomicGetAndResetCounter(&total_error_requests);
  stats.total_issued_requests =
      AtomicGetAndResetCounter(&total_issued_requests);
  for (size_t i = 0; i < load_metric_stats.size(); ++i) {
    stats.load_metric_stats.emplace_back(load_metric_stats[i].Harvest());
  }
  return stats;
}

void XdsLbClientStats::LocalityStats::AddCallStarted() {
  gpr_atm_full_fetch_add(&total_issued_requests, static_cast<gpr_atm>(1));
}

void XdsLbClientStats::LocalityStats::AddCallFinished(bool fail) {
  gpr_atm* to_update =
      fail ? &total_error_requests : &total_successful_requests;
  gpr_atm_full_fetch_add(to_update, static_cast<gpr_atm>(1));
}

//
// XdsLbClientStats::DroppedRequests
//

XdsLbClientStats::DroppedRequests XdsLbClientStats::DroppedRequests::Harvest() {
  DroppedRequests drop;
  drop.category = category;
  drop.dropped_count = AtomicGetAndResetCounter(&dropped_count);
  return drop;
}

//
// XdsLbClientStats
//

XdsLbClientStats XdsLbClientStats::Harvest() {
  XdsLbClientStats stats;
  grpc_millis now = ExecCtx::Get()->Now();
  stats.load_report_interval = now - last_report_time;
  last_report_time = now;
  stats.cluster_name = cluster_name;
  for (auto& p : upstream_locality_stats) {
    stats.upstream_locality_stats.emplace(p.first, p.second.Harvest());
  }
  stats.total_dropped_requests =
      AtomicGetAndResetCounter(&total_dropped_requests);
  for (size_t i = 0; i < dropped_requests.size(); ++i) {
    stats.dropped_requests.emplace_back(dropped_requests[i].Harvest());
  }
  return stats;
}

void XdsLbClientStats::SetLastReportTimeToNow() {
  last_report_time = ExecCtx::Get()->Now();
}

XdsLbClientStats::LocalityStats* XdsLbClientStats::FindLocalityStats(
    const RefCountedPtr<XdsLocalityName>& locality_name) {
  // FIXME: lock
  auto iter = upstream_locality_stats.find(locality_name);
  if (iter == upstream_locality_stats.end()) {
    iter =
        upstream_locality_stats.emplace(locality_name, LocalityStats()).first;
  } else {
    iter->second.Revive();
  }
  return &iter->second;
}

void XdsLbClientStats::AddCallDropped(const char* category) {
  //  // Increment num_calls_started and num_calls_finished.
  //  gpr_atm_full_fetch_add(&num_calls_started_, (gpr_atm)1);
  //  gpr_atm_full_fetch_add(&num_calls_finished_, (gpr_atm)1);
  // Record the drop.
  for (size_t i = 0; i < dropped_requests.size(); ++i) {
    if (strcmp(dropped_requests[i].category, category) == 0) {
      gpr_atm_full_fetch_add(&dropped_requests[i].dropped_count,
                             static_cast<gpr_atm>(1));
      return;
    }
  }
  // Not found, so add a new entry.
  dropped_requests.emplace_back(category, 1);
}

}  // namespace grpc_core
