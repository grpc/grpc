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

#include <string.h>

#include <grpc/support/atm.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/xds/xds_client.h"

namespace grpc_core {

//
// XdsClusterDropStats
//

XdsClusterDropStats::XdsClusterDropStats(RefCountedPtr<XdsClient> xds_client,
                                         absl::string_view lrs_server_name,
                                         absl::string_view cluster_name,
                                         absl::string_view eds_service_name)
    : xds_client_(std::move(xds_client)),
      lrs_server_name_(lrs_server_name),
      cluster_name_(cluster_name),
      eds_service_name_(eds_service_name) {}

XdsClusterDropStats::~XdsClusterDropStats() {
  xds_client_->RemoveClusterDropStats(lrs_server_name_, cluster_name_,
                                      eds_service_name_, this);
  xds_client_.reset(DEBUG_LOCATION, "DropStats");
}

XdsClusterDropStats::DroppedRequestsMap
XdsClusterDropStats::GetSnapshotAndReset() {
  MutexLock lock(&mu_);
  return std::move(dropped_requests_);
}

void XdsClusterDropStats::AddCallDropped(const std::string& category) {
  MutexLock lock(&mu_);
  ++dropped_requests_[category];
}

//
// XdsClusterLocalityStats
//

XdsClusterLocalityStats::XdsClusterLocalityStats(
    RefCountedPtr<XdsClient> xds_client, absl::string_view lrs_server_name,
    absl::string_view cluster_name, absl::string_view eds_service_name,
    RefCountedPtr<XdsLocalityName> name)
    : xds_client_(std::move(xds_client)),
      lrs_server_name_(lrs_server_name),
      cluster_name_(cluster_name),
      eds_service_name_(eds_service_name),
      name_(std::move(name)) {}

XdsClusterLocalityStats::~XdsClusterLocalityStats() {
  xds_client_->RemoveClusterLocalityStats(lrs_server_name_, cluster_name_,
                                          eds_service_name_, name_, this);
  xds_client_.reset(DEBUG_LOCATION, "LocalityStats");
}

namespace {

uint64_t GetAndResetCounter(Atomic<uint64_t>* from) {
  return from->Exchange(0, MemoryOrder::RELAXED);
}

}  // namespace

XdsClusterLocalityStats::Snapshot
XdsClusterLocalityStats::GetSnapshotAndReset() {
  Snapshot snapshot = {GetAndResetCounter(&total_successful_requests_),
                       // Don't reset total_requests_in_progress because it's
                       // not related to a single reporting interval.
                       total_requests_in_progress_.Load(MemoryOrder::RELAXED),
                       GetAndResetCounter(&total_error_requests_),
                       GetAndResetCounter(&total_issued_requests_)};
  MutexLock lock(&backend_metrics_mu_);
  snapshot.backend_metrics = std::move(backend_metrics_);
  return snapshot;
}

void XdsClusterLocalityStats::AddCallStarted() {
  total_issued_requests_.FetchAdd(1, MemoryOrder::RELAXED);
  total_requests_in_progress_.FetchAdd(1, MemoryOrder::RELAXED);
}

void XdsClusterLocalityStats::AddCallFinished(bool fail) {
  Atomic<uint64_t>& to_increment =
      fail ? total_error_requests_ : total_successful_requests_;
  to_increment.FetchAdd(1, MemoryOrder::RELAXED);
  total_requests_in_progress_.FetchAdd(-1, MemoryOrder::ACQ_REL);
}

}  // namespace grpc_core
