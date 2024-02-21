//
//
// Copyright 2018 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_client_stats.h"

#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"

namespace grpc_core {

namespace {

uint64_t GetAndResetCounter(std::atomic<uint64_t>* from) {
  return from->exchange(0, std::memory_order_relaxed);
}

}  // namespace

//
// XdsClusterDropStats
//

XdsClusterDropStats::XdsClusterDropStats(RefCountedPtr<XdsClient> xds_client,
                                         absl::string_view lrs_server,
                                         absl::string_view cluster_name,
                                         absl::string_view eds_service_name)
    : RefCounted(GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_refcount_trace)
                     ? "XdsClusterDropStats"
                     : nullptr),
      xds_client_(std::move(xds_client)),
      lrs_server_(lrs_server),
      cluster_name_(cluster_name),
      eds_service_name_(eds_service_name) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] created drop stats %p for {%s, %s, %s}",
            xds_client_.get(), this, std::string(lrs_server_).c_str(),
            std::string(cluster_name_).c_str(),
            std::string(eds_service_name_).c_str());
  }
}

XdsClusterDropStats::~XdsClusterDropStats() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] destroying drop stats %p for {%s, %s, %s}",
            xds_client_.get(), this, std::string(lrs_server_).c_str(),
            std::string(cluster_name_).c_str(),
            std::string(eds_service_name_).c_str());
  }
  xds_client_->RemoveClusterDropStats(lrs_server_, cluster_name_,
                                      eds_service_name_, this);
  xds_client_.reset(DEBUG_LOCATION, "DropStats");
}

XdsClusterDropStats::Snapshot XdsClusterDropStats::GetSnapshotAndReset() {
  Snapshot snapshot;
  snapshot.uncategorized_drops = GetAndResetCounter(&uncategorized_drops_);
  MutexLock lock(&mu_);
  snapshot.categorized_drops = std::move(categorized_drops_);
  return snapshot;
}

void XdsClusterDropStats::AddUncategorizedDrops() {
  uncategorized_drops_.fetch_add(1);
}

void XdsClusterDropStats::AddCallDropped(const std::string& category) {
  MutexLock lock(&mu_);
  ++categorized_drops_[category];
}

//
// XdsClusterLocalityStats
//

XdsClusterLocalityStats::XdsClusterLocalityStats(
    RefCountedPtr<XdsClient> xds_client, absl::string_view lrs_server,
    absl::string_view cluster_name, absl::string_view eds_service_name,
    RefCountedPtr<XdsLocalityName> name)
    : RefCounted(GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_refcount_trace)
                     ? "XdsClusterLocalityStats"
                     : nullptr),
      xds_client_(std::move(xds_client)),
      lrs_server_(lrs_server),
      cluster_name_(cluster_name),
      eds_service_name_(eds_service_name),
      name_(std::move(name)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] created locality stats %p for {%s, %s, %s, %s}",
            xds_client_.get(), this, std::string(lrs_server_).c_str(),
            std::string(cluster_name_).c_str(),
            std::string(eds_service_name_).c_str(),
            name_->AsHumanReadableString().c_str());
  }
}

XdsClusterLocalityStats::~XdsClusterLocalityStats() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] destroying locality stats %p for {%s, %s, %s, %s}",
            xds_client_.get(), this, std::string(lrs_server_).c_str(),
            std::string(cluster_name_).c_str(),
            std::string(eds_service_name_).c_str(),
            name_->AsHumanReadableString().c_str());
  }
  xds_client_->RemoveClusterLocalityStats(lrs_server_, cluster_name_,
                                          eds_service_name_, name_, this);
  xds_client_.reset(DEBUG_LOCATION, "LocalityStats");
}

XdsClusterLocalityStats::Snapshot
XdsClusterLocalityStats::GetSnapshotAndReset() {
  Snapshot snapshot;
  for (auto& percpu_stats : stats_) {
    Snapshot percpu_snapshot = {
        GetAndResetCounter(&percpu_stats.total_successful_requests),
        // Don't reset total_requests_in_progress because it's
        // not related to a single reporting interval.
        percpu_stats.total_requests_in_progress.load(std::memory_order_relaxed),
        GetAndResetCounter(&percpu_stats.total_error_requests),
        GetAndResetCounter(&percpu_stats.total_issued_requests),
        {}};
    {
      MutexLock lock(&percpu_stats.backend_metrics_mu);
      percpu_snapshot.backend_metrics = std::move(percpu_stats.backend_metrics);
    }
    snapshot += percpu_snapshot;
  }
  return snapshot;
}

void XdsClusterLocalityStats::AddCallStarted() {
  Stats& stats = stats_.this_cpu();
  stats.total_issued_requests.fetch_add(1, std::memory_order_relaxed);
  stats.total_requests_in_progress.fetch_add(1, std::memory_order_relaxed);
}

void XdsClusterLocalityStats::AddCallFinished(
    const std::map<absl::string_view, double>* named_metrics, bool fail) {
  Stats& stats = stats_.this_cpu();
  std::atomic<uint64_t>& to_increment =
      fail ? stats.total_error_requests : stats.total_successful_requests;
  to_increment.fetch_add(1, std::memory_order_relaxed);
  stats.total_requests_in_progress.fetch_add(-1, std::memory_order_acq_rel);
  if (named_metrics == nullptr) return;
  MutexLock lock(&stats.backend_metrics_mu);
  for (const auto& m : *named_metrics) {
    stats.backend_metrics[std::string(m.first)] += BackendMetric{1, m.second};
  }
}

}  // namespace grpc_core
