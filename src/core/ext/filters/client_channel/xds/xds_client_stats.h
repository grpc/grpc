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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_CLIENT_STATS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_CLIENT_STATS_H

#include <grpc/support/port_platform.h>

#include <map>

#include "absl/strings/string_view.h"

#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

// Forward declaration to avoid circular dependency.
class XdsClient;

// Locality name.
class XdsLocalityName : public RefCounted<XdsLocalityName> {
 public:
  struct Less {
    bool operator()(const XdsLocalityName* lhs,
                    const XdsLocalityName* rhs) const {
      return lhs->Compare(*rhs) < 0;
    }

    bool operator()(const RefCountedPtr<XdsLocalityName>& lhs,
                    const RefCountedPtr<XdsLocalityName>& rhs) const {
      return (*this)(lhs.get(), rhs.get());
    }
  };

  XdsLocalityName(std::string region, std::string zone, std::string subzone)
      : region_(std::move(region)),
        zone_(std::move(zone)),
        sub_zone_(std::move(subzone)) {}

  bool operator==(const XdsLocalityName& other) const {
    return region_ == other.region_ && zone_ == other.zone_ &&
           sub_zone_ == other.sub_zone_;
  }

  bool operator!=(const XdsLocalityName& other) const {
    return !(*this == other);
  }

  int Compare(const XdsLocalityName& other) const {
    int cmp_result = region_.compare(other.region_);
    if (cmp_result != 0) return cmp_result;
    cmp_result = zone_.compare(other.zone_);
    if (cmp_result != 0) return cmp_result;
    return sub_zone_.compare(other.sub_zone_);
  }

  const std::string& region() const { return region_; }
  const std::string& zone() const { return zone_; }
  const std::string& sub_zone() const { return sub_zone_; }

  const char* AsHumanReadableString() {
    if (human_readable_string_ == nullptr) {
      char* tmp;
      gpr_asprintf(&tmp, "{region=\"%s\", zone=\"%s\", sub_zone=\"%s\"}",
                   region_.c_str(), zone_.c_str(), sub_zone_.c_str());
      human_readable_string_.reset(tmp);
    }
    return human_readable_string_.get();
  }

 private:
  std::string region_;
  std::string zone_;
  std::string sub_zone_;
  UniquePtr<char> human_readable_string_;
};

// Drop stats for an xds cluster.
class XdsClusterDropStats : public RefCounted<XdsClusterDropStats> {
 public:
  using DroppedRequestsMap = std::map<std::string /* category */, uint64_t>;

  XdsClusterDropStats(RefCountedPtr<XdsClient> xds_client,
                      absl::string_view lrs_server_name,
                      absl::string_view cluster_name,
                      absl::string_view eds_service_name);
  ~XdsClusterDropStats();

  // Returns a snapshot of this instance and resets all the counters.
  DroppedRequestsMap GetSnapshotAndReset();

  void AddCallDropped(const std::string& category);

 private:
  RefCountedPtr<XdsClient> xds_client_;
  absl::string_view lrs_server_name_;
  absl::string_view cluster_name_;
  absl::string_view eds_service_name_;
  // Protects dropped_requests_. A mutex is necessary because the length of
  // dropped_requests_ can be accessed by both the picker (from data plane
  // mutex) and the load reporting thread (from the control plane combiner).
  Mutex mu_;
  DroppedRequestsMap dropped_requests_;
};

// Locality stats for an xds cluster.
class XdsClusterLocalityStats : public RefCounted<XdsClusterLocalityStats> {
 public:
  struct BackendMetric {
    uint64_t num_requests_finished_with_metric;
    double total_metric_value;

    BackendMetric& operator+=(const BackendMetric& other) {
      num_requests_finished_with_metric +=
          other.num_requests_finished_with_metric;
      total_metric_value += other.total_metric_value;
      return *this;
    }

    bool IsZero() const {
      return num_requests_finished_with_metric == 0 && total_metric_value == 0;
    }
  };

  struct Snapshot {
    uint64_t total_successful_requests;
    uint64_t total_requests_in_progress;
    uint64_t total_error_requests;
    uint64_t total_issued_requests;
    std::map<std::string, BackendMetric> backend_metrics;

    Snapshot& operator+=(const Snapshot& other) {
      total_successful_requests += other.total_successful_requests;
      total_requests_in_progress += other.total_requests_in_progress;
      total_error_requests += other.total_error_requests;
      total_issued_requests += other.total_issued_requests;
      for (const auto& p : other.backend_metrics) {
        backend_metrics[p.first] += p.second;
      }
      return *this;
    }

    bool IsZero() const {
      if (total_successful_requests != 0 || total_requests_in_progress != 0 ||
          total_error_requests != 0 || total_issued_requests != 0) {
        return false;
      }
      for (const auto& p : backend_metrics) {
        if (!p.second.IsZero()) return false;
      }
      return true;
    }
  };

  XdsClusterLocalityStats(RefCountedPtr<XdsClient> xds_client,
                          absl::string_view lrs_server_name,
                          absl::string_view cluster_name,
                          absl::string_view eds_service_name,
                          RefCountedPtr<XdsLocalityName> name);
  ~XdsClusterLocalityStats();

  // Returns a snapshot of this instance and resets all the counters.
  Snapshot GetSnapshotAndReset();

  void AddCallStarted();
  void AddCallFinished(bool fail = false);

 private:
  RefCountedPtr<XdsClient> xds_client_;
  absl::string_view lrs_server_name_;
  absl::string_view cluster_name_;
  absl::string_view eds_service_name_;
  RefCountedPtr<XdsLocalityName> name_;

  Atomic<uint64_t> total_successful_requests_{0};
  Atomic<uint64_t> total_requests_in_progress_{0};
  Atomic<uint64_t> total_error_requests_{0};
  Atomic<uint64_t> total_issued_requests_{0};

  // Protects backend_metrics_. A mutex is necessary because the length of
  // backend_metrics_ can be accessed by both the callback intercepting the
  // call's recv_trailing_metadata (not from the control plane work serializer)
  // and the load reporting thread (from the control plane work serializer).
  Mutex backend_metrics_mu_;
  std::map<std::string, BackendMetric> backend_metrics_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_CLIENT_STATS_H */
