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

#ifndef GRPC_SRC_CORE_EXT_XDS_XDS_CLIENT_STATS_H
#define GRPC_SRC_CORE_EXT_XDS_XDS_CLIENT_STATS_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/per_cpu.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/resolver/endpoint_addresses.h"

namespace grpc_core {

// Forward declaration to avoid circular dependency.
class XdsClient;

// Locality name.
class XdsLocalityName final : public RefCounted<XdsLocalityName> {
 public:
  struct Less {
    bool operator()(const XdsLocalityName* lhs,
                    const XdsLocalityName* rhs) const {
      if (lhs == nullptr || rhs == nullptr) return QsortCompare(lhs, rhs);
      return lhs->Compare(*rhs) < 0;
    }

    bool operator()(const RefCountedPtr<XdsLocalityName>& lhs,
                    const RefCountedPtr<XdsLocalityName>& rhs) const {
      return (*this)(lhs.get(), rhs.get());
    }
  };

  XdsLocalityName(std::string region, std::string zone, std::string sub_zone)
      : region_(std::move(region)),
        zone_(std::move(zone)),
        sub_zone_(std::move(sub_zone)),
        human_readable_string_(
            absl::StrFormat("{region=\"%s\", zone=\"%s\", sub_zone=\"%s\"}",
                            region_, zone_, sub_zone_)) {}

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

  const RefCountedStringValue& human_readable_string() const {
    return human_readable_string_;
  }

  // Channel args traits.
  static absl::string_view ChannelArgName() {
    return GRPC_ARG_NO_SUBCHANNEL_PREFIX "xds_locality_name";
  }
  static int ChannelArgsCompare(const XdsLocalityName* a,
                                const XdsLocalityName* b) {
    return a->Compare(*b);
  }

 private:
  std::string region_;
  std::string zone_;
  std::string sub_zone_;
  RefCountedStringValue human_readable_string_;
};

// Drop stats for an xds cluster.
class XdsClusterDropStats final : public RefCounted<XdsClusterDropStats> {
 public:
  // The total number of requests dropped for any reason is the sum of
  // uncategorized_drops, and dropped_requests map.
  using CategorizedDropsMap = std::map<std::string /* category */, uint64_t>;
  struct Snapshot {
    uint64_t uncategorized_drops = 0;
    // The number of requests dropped for the specific drop categories
    // outlined in the drop_overloads field in the EDS response.
    CategorizedDropsMap categorized_drops;

    Snapshot& operator+=(const Snapshot& other) {
      uncategorized_drops += other.uncategorized_drops;
      for (const auto& p : other.categorized_drops) {
        categorized_drops[p.first] += p.second;
      }
      return *this;
    }

    bool IsZero() const {
      if (uncategorized_drops != 0) return false;
      for (const auto& p : categorized_drops) {
        if (p.second != 0) return false;
      }
      return true;
    }
  };

  XdsClusterDropStats(RefCountedPtr<XdsClient> xds_client,
                      absl::string_view lrs_server,
                      absl::string_view cluster_name,
                      absl::string_view eds_service_name);
  ~XdsClusterDropStats() override;

  // Returns a snapshot of this instance and resets all the counters.
  Snapshot GetSnapshotAndReset();

  void AddUncategorizedDrops();
  void AddCallDropped(const std::string& category);

 private:
  RefCountedPtr<XdsClient> xds_client_;
  absl::string_view lrs_server_;
  absl::string_view cluster_name_;
  absl::string_view eds_service_name_;
  std::atomic<uint64_t> uncategorized_drops_{0};
  // Protects categorized_drops_. A mutex is necessary because the length of
  // dropped_requests can be accessed by both the picker (from data plane
  // mutex) and the load reporting thread (from the control plane combiner).
  Mutex mu_;
  CategorizedDropsMap categorized_drops_ ABSL_GUARDED_BY(mu_);
};

// Locality stats for an xds cluster.
class XdsClusterLocalityStats final
    : public RefCounted<XdsClusterLocalityStats> {
 public:
  struct BackendMetric {
    uint64_t num_requests_finished_with_metric = 0;
    double total_metric_value = 0;

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
    uint64_t total_successful_requests = 0;
    uint64_t total_requests_in_progress = 0;
    uint64_t total_error_requests = 0;
    uint64_t total_issued_requests = 0;
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
                          absl::string_view lrs_server,
                          absl::string_view cluster_name,
                          absl::string_view eds_service_name,
                          RefCountedPtr<XdsLocalityName> name);
  ~XdsClusterLocalityStats() override;

  // Returns a snapshot of this instance and resets all the counters.
  Snapshot GetSnapshotAndReset();

  void AddCallStarted();
  void AddCallFinished(const std::map<absl::string_view, double>* named_metrics,
                       bool fail = false);

  XdsLocalityName* locality_name() const { return name_.get(); }

 private:
  struct Stats {
    std::atomic<uint64_t> total_successful_requests{0};
    std::atomic<uint64_t> total_requests_in_progress{0};
    std::atomic<uint64_t> total_error_requests{0};
    std::atomic<uint64_t> total_issued_requests{0};

    // Protects backend_metrics. A mutex is necessary because the length of
    // backend_metrics_ can be accessed by both the callback intercepting the
    // call's recv_trailing_metadata and the load reporting thread.
    Mutex backend_metrics_mu;
    std::map<std::string, BackendMetric> backend_metrics
        ABSL_GUARDED_BY(backend_metrics_mu);
  };

  RefCountedPtr<XdsClient> xds_client_;
  absl::string_view lrs_server_;
  absl::string_view cluster_name_;
  absl::string_view eds_service_name_;
  RefCountedPtr<XdsLocalityName> name_;
  PerCpu<Stats> stats_{PerCpuOptions().SetMaxShards(32).SetCpusPerShard(4)};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_XDS_XDS_CLIENT_STATS_H
