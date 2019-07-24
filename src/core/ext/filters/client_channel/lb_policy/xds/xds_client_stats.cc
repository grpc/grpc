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

template <typename T>
T GetAndResetCounter(Atomic<T>* from) {
  return from->Exchange(0, MemoryOrder::RELAXED);
}

}  // namespace

//
// XdsLbClientStats::LocalityStats::LoadMetric::Snapshot
//

bool XdsLbClientStats::LocalityStats::LoadMetric::Snapshot::IsAllZero() const {
  return total_metric_value == 0 && num_requests_finished_with_metric == 0;
}

//
// XdsLbClientStats::LocalityStats::LoadMetric
//

XdsLbClientStats::LocalityStats::LoadMetric::Snapshot
XdsLbClientStats::LocalityStats::LoadMetric::GetSnapshotAndReset() {
  Snapshot metric = {num_requests_finished_with_metric_, total_metric_value_};
  num_requests_finished_with_metric_ = 0;
  total_metric_value_ = 0;
  return metric;
}

//
// XdsLbClientStats::LocalityStats::Snapshot
//

bool XdsLbClientStats::LocalityStats::Snapshot::IsAllZero() {
  if (total_successful_requests != 0 || total_requests_in_progress != 0 ||
      total_error_requests != 0 || total_issued_requests != 0) {
    return false;
  }
  for (auto& p : load_metric_stats) {
    const LoadMetric::Snapshot& metric_value = p.second;
    if (!metric_value.IsAllZero()) return false;
  }
  return true;
}

//
// XdsLbClientStats::LocalityStats
//

XdsLbClientStats::LocalityStats::Snapshot
XdsLbClientStats::LocalityStats::GetSnapshotAndReset() {
  Snapshot snapshot = {
      GetAndResetCounter(&total_successful_requests_),
      // Don't reset total_requests_in_progress because it's not
      // related to a single reporting interval.
      total_requests_in_progress_.Load(MemoryOrder::RELAXED),
      GetAndResetCounter(&total_error_requests_),
      GetAndResetCounter(&total_issued_requests_)};
  {
    MutexLock lock(&load_metric_stats_mu_);
    for (auto& p : load_metric_stats_) {
      const char* metric_name = p.first.get();
      LoadMetric& metric_value = p.second;
      snapshot.load_metric_stats.emplace(gpr_strdup(metric_name),
                                         metric_value.GetSnapshotAndReset());
    }
  }
  return snapshot;
}

void XdsLbClientStats::LocalityStats::AddCallStarted() {
  total_issued_requests_.FetchAdd(1, MemoryOrder::RELAXED);
  total_requests_in_progress_.FetchAdd(1, MemoryOrder::RELAXED);
}

void XdsLbClientStats::LocalityStats::AddCallFinished(bool fail) {
  Atomic<uint64_t>& to_increment =
      fail ? total_error_requests_ : total_successful_requests_;
  to_increment.FetchAdd(1, MemoryOrder::RELAXED);
  total_requests_in_progress_.FetchAdd(-1, MemoryOrder::ACQ_REL);
}

//
// XdsLbClientStats::Snapshot
//

bool XdsLbClientStats::Snapshot::IsAllZero() {
  for (auto& p : upstream_locality_stats) {
    if (!p.second.IsAllZero()) return false;
  }
  for (auto& p : dropped_requests) {
    if (p.second != 0) return false;
  }
  return total_dropped_requests == 0;
}

//
// XdsLbClientStats
//

XdsLbClientStats::Snapshot XdsLbClientStats::GetSnapshotAndReset() {
  grpc_millis now = ExecCtx::Get()->Now();
  // Record total_dropped_requests and reporting interval in the snapshot.
  Snapshot snapshot = {
      .total_dropped_requests = GetAndResetCounter(&total_dropped_requests_),
      .load_report_interval = now - last_report_time_};
  // Update last report time.
  last_report_time_ = now;
  // Snapshot all the other stats.
  for (auto& p : upstream_locality_stats_) {
    snapshot.upstream_locality_stats.emplace(p.first,
                                             p.second->GetSnapshotAndReset());
  }
  {
    MutexLock lock(&dropped_requests_mu_);
    snapshot.dropped_requests = std::move(dropped_requests_);
  }
  return snapshot;
}

void XdsLbClientStats::MaybeInitLastReportTime() {
  static bool inited = false;
  if (inited) return;
  last_report_time_ = ExecCtx::Get()->Now();
  inited = true;
}

XdsLbClientStats::LocalityStats* XdsLbClientStats::FindLocalityStats(
    const RefCountedPtr<XdsLocalityName>& locality_name) {
  auto iter = upstream_locality_stats_.find(locality_name);
  if (iter == upstream_locality_stats_.end()) {
    iter = upstream_locality_stats_
               .emplace(locality_name, MakeUnique<LocalityStats>())
               .first;
  }
  return iter->second.get();
}

void XdsLbClientStats::PruneLocalityStats() {
  auto iter = upstream_locality_stats_.begin();
  while (iter != upstream_locality_stats_.end()) {
    if (iter->second->IsSafeToDelete()) {
      iter = upstream_locality_stats_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void XdsLbClientStats::AddCallDropped(const UniquePtr<char>& category) {
  total_dropped_requests_.FetchAdd(1, MemoryOrder::RELAXED);
  MutexLock lock(&dropped_requests_mu_);
  auto iter = dropped_requests_.find(category);
  if (iter == dropped_requests_.end()) {
    dropped_requests_.emplace(UniquePtr<char>(gpr_strdup(category.get())), 1);
  } else {
    ++iter->second;
  }
}

}  // namespace grpc_core
