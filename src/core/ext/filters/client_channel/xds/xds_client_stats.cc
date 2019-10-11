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

#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"

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
// XdsClientStats::LocalityStats::LoadMetric::Snapshot
//

bool XdsClientStats::LocalityStats::LoadMetric::Snapshot::IsAllZero() const {
  return total_metric_value == 0 && num_requests_finished_with_metric == 0;
}

//
// XdsClientStats::LocalityStats::LoadMetric
//

XdsClientStats::LocalityStats::LoadMetric::Snapshot
XdsClientStats::LocalityStats::LoadMetric::GetSnapshotAndReset() {
  Snapshot metric = {num_requests_finished_with_metric_, total_metric_value_};
  num_requests_finished_with_metric_ = 0;
  total_metric_value_ = 0;
  return metric;
}

//
// XdsClientStats::LocalityStats::Snapshot
//

bool XdsClientStats::LocalityStats::Snapshot::IsAllZero() {
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
// XdsClientStats::LocalityStats
//

XdsClientStats::LocalityStats::Snapshot
XdsClientStats::LocalityStats::GetSnapshotAndReset() {
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
      snapshot.load_metric_stats.emplace(
          UniquePtr<char>(gpr_strdup(metric_name)),
          metric_value.GetSnapshotAndReset());
    }
  }
  return snapshot;
}

void XdsClientStats::LocalityStats::AddCallStarted() {
  total_issued_requests_.FetchAdd(1, MemoryOrder::RELAXED);
  total_requests_in_progress_.FetchAdd(1, MemoryOrder::RELAXED);
}

void XdsClientStats::LocalityStats::AddCallFinished(bool fail) {
  Atomic<uint64_t>& to_increment =
      fail ? total_error_requests_ : total_successful_requests_;
  to_increment.FetchAdd(1, MemoryOrder::RELAXED);
  total_requests_in_progress_.FetchAdd(-1, MemoryOrder::ACQ_REL);
}

//
// XdsClientStats::Snapshot
//

bool XdsClientStats::Snapshot::IsAllZero() {
  for (auto& p : upstream_locality_stats) {
    if (!p.second.IsAllZero()) return false;
  }
  for (auto& p : dropped_requests) {
    if (p.second != 0) return false;
  }
  return total_dropped_requests == 0;
}

//
// XdsClientStats
//

XdsClientStats::Snapshot XdsClientStats::GetSnapshotAndReset() {
  grpc_millis now = ExecCtx::Get()->Now();
  // Record total_dropped_requests and reporting interval in the snapshot.
  Snapshot snapshot;
  snapshot.total_dropped_requests =
      GetAndResetCounter(&total_dropped_requests_);
  snapshot.load_report_interval = now - last_report_time_;
  // Update last report time.
  last_report_time_ = now;
  // Snapshot all the other stats.
  for (auto& p : upstream_locality_stats_) {
    snapshot.upstream_locality_stats.emplace(p.first,
                                             p.second->GetSnapshotAndReset());
  }
  {
    MutexLock lock(&dropped_requests_mu_);
    // This is a workaround for the case where some compilers cannot build
    // move-assignment of map with non-copyable but movable key.
    // https://stackoverflow.com/questions/36475497
    std::swap(snapshot.dropped_requests, dropped_requests_);
    dropped_requests_.clear();
  }
  return snapshot;
}

void XdsClientStats::MaybeInitLastReportTime() {
  if (last_report_time_ == -1) last_report_time_ = ExecCtx::Get()->Now();
}

RefCountedPtr<XdsClientStats::LocalityStats> XdsClientStats::FindLocalityStats(
    const RefCountedPtr<XdsLocalityName>& locality_name) {
  auto iter = upstream_locality_stats_.find(locality_name);
  if (iter == upstream_locality_stats_.end()) {
    iter = upstream_locality_stats_
               .emplace(locality_name, MakeRefCounted<LocalityStats>())
               .first;
  }
  return iter->second;
}

void XdsClientStats::PruneLocalityStats() {
  auto iter = upstream_locality_stats_.begin();
  while (iter != upstream_locality_stats_.end()) {
    if (iter->second->IsSafeToDelete()) {
      iter = upstream_locality_stats_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void XdsClientStats::AddCallDropped(const UniquePtr<char>& category) {
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
