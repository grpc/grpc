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
void GetAndResetCounter(Atomic<T>* from, Atomic<T>* to) {
  T count = from->Exchange(0, MemoryOrder::ACQ_REL);
  to->Store(count, MemoryOrder::ACQ_REL);
}

}  // namespace

//
// XdsLbClientStats::LocalityStats::LoadMetric
//

XdsLbClientStats::LocalityStats::LoadMetric::LoadMetric(
    XdsLbClientStats::LocalityStats::LoadMetric&& other) noexcept
    : metric_name_(std::move(other.metric_name_)) {
  GetAndResetCounter(&num_requests_finished_with_metric_,
                     &other.num_requests_finished_with_metric_);
  GetAndResetCounter(&total_metric_value_, &other.total_metric_value_);
}

XdsLbClientStats::LocalityStats::LoadMetric
XdsLbClientStats::LocalityStats::LoadMetric::Harvest() {
  LoadMetric metric;
  metric.metric_name_.reset(gpr_strdup(metric_name()));
  GetAndResetCounter(&num_requests_finished_with_metric_,
                     &metric.num_requests_finished_with_metric_);
  GetAndResetCounter(&total_metric_value_, &metric.total_metric_value_);
  return metric;
}

bool XdsLbClientStats::LocalityStats::LoadMetric::IsAllZero() const {
  return total_metric_value_.Load(MemoryOrder::ACQ_REL) == 0 &&
         num_requests_finished_with_metric_.Load(MemoryOrder::ACQ_REL) == 0;
}

//
// XdsLbClientStats::LocalityStats
//

XdsLbClientStats::LocalityStats::LocalityStats(
    XdsLbClientStats::LocalityStats&& other) noexcept {
  *this = std::move(other);
}

XdsLbClientStats::LocalityStats& XdsLbClientStats::LocalityStats::operator=(
    grpc_core::XdsLbClientStats::LocalityStats&& other) noexcept {
  GetAndResetCounter(&other.total_successful_requests_,
                     &total_successful_requests_);
  GetAndResetCounter(&other.total_requests_in_progress_,
                     &total_requests_in_progress_);
  GetAndResetCounter(&other.total_error_requests_, &total_error_requests_);
  GetAndResetCounter(&other.total_issued_requests_, &total_issued_requests_);
  load_metric_stats_ = std::move(other.load_metric_stats_);
  return *this;
}

XdsLbClientStats::LocalityStats XdsLbClientStats::LocalityStats::Harvest() {
  LocalityStats stats;
  GetAndResetCounter(&total_successful_requests_,
                     &stats.total_successful_requests_);
  // Don't reset total_requests_in_progress because it's not related to a single
  // reporting interval.
  stats.total_requests_in_progress_.Store(
      total_requests_in_progress_.Load(MemoryOrder::ACQ_REL),
      MemoryOrder::ACQ_REL);
  GetAndResetCounter(&total_error_requests_, &stats.total_error_requests_);
  GetAndResetCounter(&total_issued_requests_, &stats.total_issued_requests_);
  for (size_t i = 0; i < load_metric_stats_.size(); ++i) {
    stats.load_metric_stats_.emplace_back(load_metric_stats_[i].Harvest());
  }
  return stats;
}

bool XdsLbClientStats::LocalityStats::IsAllZero() const {
  if (total_successful_requests_.Load(MemoryOrder::ACQ_REL) != 0 ||
      total_requests_in_progress_.Load(MemoryOrder::ACQ_REL) != 0 ||
      total_error_requests_.Load(MemoryOrder::ACQ_REL) != 0 ||
      total_issued_requests_.Load(MemoryOrder::ACQ_REL) != 0) {
    return false;
  }
  for (size_t i = 0; i < load_metric_stats_.size(); ++i) {
    if (load_metric_stats_[i].IsAllZero()) return false;
  }
  return true;
}

void XdsLbClientStats::LocalityStats::AddCallStarted() {
  if (dying_) {
    gpr_log(GPR_ERROR, "Can't record call starting on dying locality stats %p",
            this);
    return;
  }
  total_issued_requests_.FetchAdd(1);
  total_requests_in_progress_.FetchAdd(1);
}

void XdsLbClientStats::LocalityStats::AddCallFinished(bool fail) {
  Atomic<uint64_t>& to_increment =
      fail ? total_error_requests_ : total_successful_requests_;
  to_increment.FetchAdd(1);
  total_requests_in_progress_.FetchAdd(-1);
}

//
// XdsLbClientStats
//

XdsLbClientStats::XdsLbClientStats(grpc_core::XdsLbClientStats&& other) noexcept
    : upstream_locality_stats_(std::move(other.upstream_locality_stats_)),
      dropped_requests_(std::move(other.dropped_requests_)),
      load_report_interval_(other.load_report_interval_),
      last_report_time_(other.last_report_time_) {
  GetAndResetCounter(&total_dropped_requests_, &other.total_dropped_requests_);
}

XdsLbClientStats XdsLbClientStats::Harvest() {
  XdsLbClientStats stats;
  // Record reporting interval in the harvest.
  grpc_millis now = ExecCtx::Get()->Now();
  stats.load_report_interval_ = now - last_report_time_;
  // Update last report time.
  last_report_time_ = now;
  // Harvest all the stats.
  for (auto& p : upstream_locality_stats_) {
    stats.upstream_locality_stats_.emplace(p.first, p.second.Harvest());
  }
  GetAndResetCounter(&total_dropped_requests_, &stats.total_dropped_requests_);
  {
    MutexLock lock(&dropped_requests_mu_);
    stats.dropped_requests_ = dropped_requests_;
    for (auto& p : dropped_requests_) p.second = 0;
  }
  return stats;
}

bool XdsLbClientStats::IsAllZero() {
  for (auto& p : upstream_locality_stats_) {
    if (!p.second.IsAllZero()) return false;
  }
  return total_dropped_requests_.Load(MemoryOrder::ACQ_REL) == 0;
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
