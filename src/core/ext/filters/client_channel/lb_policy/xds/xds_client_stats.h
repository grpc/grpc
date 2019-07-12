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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_CLIENT_STATS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_CLIENT_STATS_H

#include <grpc/support/port_platform.h>

#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

constexpr char kCpuUtilization[] = "cpu_utilization";

class XdsLocalityName : public RefCounted<XdsLocalityName> {
 public:
  struct Less {
    bool operator()(const RefCountedPtr<XdsLocalityName>& lhs,
                    const RefCountedPtr<XdsLocalityName>& rhs) {
      int cmp_result = strcmp(lhs->region_.get(), rhs->region_.get());
      if (cmp_result != 0) return cmp_result < 0;
      cmp_result = strcmp(lhs->zone_.get(), rhs->zone_.get());
      if (cmp_result != 0) return cmp_result < 0;
      return strcmp(lhs->sub_zone_.get(), rhs->sub_zone_.get()) < 0;
    }
  };

  XdsLocalityName(UniquePtr<char> region, UniquePtr<char> zone,
                  UniquePtr<char> subzone)
      : region_(std::move(region)),
        zone_(std::move(zone)),
        sub_zone_(std::move(subzone)) {}

  bool operator==(const XdsLocalityName& other) const {
    return strcmp(region_.get(), other.region_.get()) == 0 &&
           strcmp(zone_.get(), other.zone_.get()) == 0 &&
           strcmp(sub_zone_.get(), other.sub_zone_.get()) == 0;
  }

  // FIXME: should be null terminated
  const char* region() const { return region_.get(); }
  const char* zone() const { return zone_.get(); }
  const char* sub_zone() const { return sub_zone_.get(); }

  const char* AsHumanReadableString() {
    if (human_readable_string_ == nullptr) {
      char* tmp;
      gpr_asprintf(&tmp, "{region=\"%s\", zone=\"%s\", sub_zone=\"%s\"}",
                   region_.get(), zone_.get(), sub_zone_.get());
      human_readable_string_.reset(tmp);
    }
    return human_readable_string_.get();
  }

 private:
  UniquePtr<char> region_;
  UniquePtr<char> zone_;
  UniquePtr<char> sub_zone_;
  UniquePtr<char> human_readable_string_;
};

// The stats classes (i.e., XdsLbClientStats, LocalityStats, and LoadMetric) can
// be Reap()ed to populate the load report. The reaped results are contained in
// the respective Harvest structs. The Harvest structs have no synchronization.
// The stats classes use several different synchronization methods. 1. Most of
// the counters are Atomic<>s for performance. 2. Some of the Map<>s are
// protected by Mutex if we are not guaranteed that the accesses to them are
// synchronized by the callers. 3. The Map<>s to which the accesses are already
// synchronized by the callers do not have additional synchronization here.
// FIXME: audit the memory order usage.
class XdsLbClientStats {
 public:
  class LocalityStats {
   public:
    class LoadMetric {
     public:
      struct Harvest {
        bool IsAllZero() const;

        uint64_t num_requests_finished_with_metric;
        double total_metric_value;
      };

      // Returns a snapshot of this instance and reset all the accumulative
      // counters.
      Harvest Reap();

     private:
      uint64_t num_requests_finished_with_metric_{0};
      double total_metric_value_{0};
    };

    using LoadMetricMap = Map<UniquePtr<char>, LoadMetric, StringLess>;
    using LoadMetricHarvestMap =
        Map<UniquePtr<char>, LoadMetric::Harvest, StringLess>;

    struct Harvest {
      // TODO(juanlishen): Change this to const method when const_iterator is
      // added to Map<>.
      bool IsAllZero();

      uint64_t total_successful_requests;
      uint64_t total_requests_in_progress;
      uint64_t total_error_requests;
      uint64_t total_issued_requests;
      LoadMetricHarvestMap load_metric_stats;
    };

    LocalityStats() = default;
    // Move ctor. For map operations.
    LocalityStats(LocalityStats&& other) noexcept;
    // Copy assignment. For map operations.
    LocalityStats& operator=(LocalityStats&& other) noexcept;

    // Returns a snapshot of this instance and reset all the accumulative
    // counters.
    Harvest Reap();

    // After a LocalityStats is killed, it can't call AddCallStarted() unless
    // revived. AddCallFinished() can still be called. Once the number of in
    // progress calls drops to 0, this LocalityStats can be deleted.
    void Kill() { dying_ = true; }
    void Revive() { dying_ = false; }
    bool IsSafeToDelete() {
      return dying_ &&
             total_requests_in_progress_.Load(MemoryOrder::ACQ_REL) == 0;
    }

    void AddCallStarted();
    void AddCallFinished(bool fail = false);

   private:
    Atomic<uint64_t> total_successful_requests_{0};
    Atomic<uint64_t> total_requests_in_progress_{0};
    // Requests that were issued (not dropped) but failed.
    Atomic<uint64_t> total_error_requests_{0};
    Atomic<uint64_t> total_issued_requests_{0};
    // Protects load_metric_stats_. A mutex is necessary because the length of
    // load_metric_stats_ can be accessed by both the callback intercepting the
    // call's recv_trailing_metadata (not from any combiner) and the load
    // reporting thread (from the control plane combiner).
    Mutex load_metric_stats_mu_;
    LoadMetricMap load_metric_stats_;
    bool dying_ = false;
  };

  using LocalityStatsMap =
      Map<RefCountedPtr<XdsLocalityName>, LocalityStats, XdsLocalityName::Less>;
  using LocalityStatsHarvestMap =
      Map<RefCountedPtr<XdsLocalityName>, LocalityStats::Harvest,
          XdsLocalityName::Less>;
  using DroppedRequestsMap = Map<UniquePtr<char>, uint64_t, StringLess>;
  using DroppedRequestsHarvestMap = DroppedRequestsMap;

  struct Harvest {
    // TODO(juanlishen): Change this to const method when const_iterator is
    // added to Map<>.
    bool IsAllZero();

    LocalityStatsHarvestMap upstream_locality_stats;
    uint64_t total_dropped_requests;
    DroppedRequestsHarvestMap dropped_requests;
    // The actual load report interval.
    grpc_millis load_report_interval;
  };

  // Returns a snapshot of this instance and reset all the accumulative
  // counters.
  Harvest Reap();

  void MaybeInitLastReportTime();
  LocalityStats* FindLocalityStats(
      const RefCountedPtr<XdsLocalityName>& locality_name);
  void PruneLocalityStats();
  void AddCallDropped(UniquePtr<char> category);

 private:
  // The stats for each locality.
  LocalityStatsMap upstream_locality_stats_;
  Atomic<uint64_t> total_dropped_requests_{0};
  // Protects dropped_requests_. A mutex is necessary because the length of
  // dropped_requests_ can be accessed by both the picker (from data plane
  // combiner) and the load reporting thread (from the control plane combiner).
  Mutex dropped_requests_mu_;
  DroppedRequestsMap dropped_requests_;
  // The timestamp of last reporting. For the LB-policy-wide first report, the
  // last_report_time is the time we scheduled the first reporting timer.
  grpc_millis last_report_time_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_CLIENT_STATS_H \
        */
