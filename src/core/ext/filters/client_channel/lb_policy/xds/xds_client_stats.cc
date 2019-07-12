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

template <typename T>
void CopyCounter(Atomic<T>* from, Atomic<T>* to) {
  T count = from->Load(MemoryOrder::RELAXED);
  to->Store(count, MemoryOrder::RELAXED);
}

}  // namespace

//
// XdsLbClientStats::LocalityStats::LoadMetric::Harvest
//

bool XdsLbClientStats::LocalityStats::LoadMetric::Harvest::IsAllZero() const {
  return total_metric_value == 0 && num_requests_finished_with_metric == 0;
}

//
// XdsLbClientStats::LocalityStats::LoadMetric
//

XdsLbClientStats::LocalityStats::LoadMetric::Harvest
XdsLbClientStats::LocalityStats::LoadMetric::Reap() {
  Harvest metric = {num_requests_finished_with_metric_, total_metric_value_};
  num_requests_finished_with_metric_ = 0;
  total_metric_value_ = 0;
  return metric;
}

//
// XdsLbClientStats::LocalityStats::Harvest
//

bool XdsLbClientStats::LocalityStats::Harvest::IsAllZero() {
  if (total_successful_requests != 0 || total_requests_in_progress != 0 ||
      total_error_requests != 0 || total_issued_requests != 0) {
    return false;
  }
  for (auto& p : load_metric_stats) {
    const LoadMetric::Harvest& metric_value = p.second;
    if (!metric_value.IsAllZero()) return false;
  }
  return true;
}

//
// XdsLbClientStats::LocalityStats
//

// FIXME: What happens when the tree is rebalancing? Fix potential
// synchronization issue.
XdsLbClientStats::LocalityStats::LocalityStats(
    XdsLbClientStats::LocalityStats&& other) noexcept
    : total_successful_requests_(
          GetAndResetCounter(&other.total_successful_requests_)),
      total_requests_in_progress_(
          GetAndResetCounter(&other.total_requests_in_progress_)),
      total_error_requests_(GetAndResetCounter(&other.total_error_requests_)),
      total_issued_requests_(
          GetAndResetCounter(&other.total_issued_requests_)) {
  MutexLock lock(&other.load_metric_stats_mu_);
  load_metric_stats_ = other.load_metric_stats_;
}

XdsLbClientStats::LocalityStats& XdsLbClientStats::LocalityStats::operator=(
    grpc_core::XdsLbClientStats::LocalityStats&& other) noexcept {
  CopyCounter(&other.total_successful_requests_, &total_successful_requests_);
  CopyCounter(&other.total_requests_in_progress_, &total_requests_in_progress_);
  CopyCounter(&other.total_error_requests_, &total_error_requests_);
  CopyCounter(&other.total_issued_requests_, &total_issued_requests_);
  load_metric_stats_ = other.load_metric_stats_;
  return *this;
}

XdsLbClientStats::LocalityStats::Harvest
XdsLbClientStats::LocalityStats::Reap() {
  Harvest harvest = {GetAndResetCounter(&total_successful_requests_),
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
      harvest.load_metric_stats.emplace(gpr_strdup(metric_name),
                                        metric_value.Reap());
    }
  }
  return harvest;
}

void XdsLbClientStats::LocalityStats::AddCallStarted() {
  if (dying_) {
    gpr_log(GPR_ERROR, "Can't record call starting on dying locality stats %p",
            this);
    return;
  }
  total_issued_requests_.FetchAdd(1, MemoryOrder::RELAXED);
  total_requests_in_progress_.FetchAdd(1, MemoryOrder::RELAXED);
}

void XdsLbClientStats::LocalityStats::AddCallFinished(bool fail) {
  Atomic<uint64_t>& to_increment =
      fail ? total_error_requests_ : total_successful_requests_;
  to_increment.FetchAdd(1, MemoryOrder::ACQ_REL);
  total_requests_in_progress_.FetchAdd(-1, MemoryOrder::ACQ_REL);
}

//
// XdsLbClientStats::Harvest
//

bool XdsLbClientStats::Harvest::IsAllZero() {
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

XdsLbClientStats::Harvest XdsLbClientStats::Reap() {
  grpc_millis now = ExecCtx::Get()->Now();
  // Record total_dropped_requests and reporting interval in the harvest.
  Harvest harvest = {
      .total_dropped_requests = GetAndResetCounter(&total_dropped_requests_),
      .load_report_interval = now - last_report_time_};
  // Update last report time.
  last_report_time_ = now;
  // Harvest all the other stats.
  for (auto& p : upstream_locality_stats_) {
    harvest.upstream_locality_stats.emplace(p.first, p.second.Reap());
  }
  {
    MutexLock lock(&dropped_requests_mu_);
    harvest.dropped_requests = dropped_requests_;
    for (auto& p : dropped_requests_) p.second = 0;
  }
  return harvest;
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
    iter =
        upstream_locality_stats_.emplace(locality_name, LocalityStats()).first;
  } else {
    iter->second.Revive();
  }
  return &iter->second;
}

void XdsLbClientStats::PruneLocalityStats() {
  auto iter = upstream_locality_stats_.begin();
  while (iter != upstream_locality_stats_.end()) {
    if (iter->second.IsSafeToDelete()) {
      iter = upstream_locality_stats_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void XdsLbClientStats::AddCallDropped(UniquePtr<char> category) {
  MutexLock lock(&dropped_requests_mu_);
  auto iter = dropped_requests_.find(category);
  if (iter == dropped_requests_.end()) iter = dropped_requests_.emplace().first;
  ++iter->second;
}

}  // namespace grpc_core
