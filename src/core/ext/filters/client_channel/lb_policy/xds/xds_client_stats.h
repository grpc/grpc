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

// Thread-safe on data plane; thread-unsafe on control plane.
// FIXME: audit the memory order usage.
class XdsLbClientStats {
 public:
  class LocalityStats {
   public:
    class LoadMetric {
     public:
      LoadMetric() = default;
      LoadMetric(LoadMetric&& other) noexcept;

      // Returns a snapshot of this instance and reset all the accumulative
      // counters.
      LoadMetric Harvest();
      bool IsAllZero() const;

      const char* metric_name() const { return metric_name_.get(); }
      uint64_t num_requests_finished_with_metric() const {
        return num_requests_finished_with_metric_.Load(MemoryOrder::RELAXED);
      }
      double total_metric_value() const {
        return total_metric_value_.Load(MemoryOrder::RELAXED);
      }

     private:
      UniquePtr<char> metric_name_;
      Atomic<uint64_t> num_requests_finished_with_metric_{0};
      Atomic<double> total_metric_value_{0};
    };

    using LoadMetricList = InlinedVector<LoadMetric, 1>;

    LocalityStats() = default;
    LocalityStats(LocalityStats&& other) noexcept;
    // For map operations.
    LocalityStats& operator=(LocalityStats&& other) noexcept;

    // Returns a snapshot of this instance and reset all the accumulative
    // counters.
    LocalityStats Harvest();
    bool IsAllZero() const;

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

    uint64_t total_successful_requests() const {
      return total_successful_requests_.Load(MemoryOrder::RELAXED);
    }
    uint64_t total_requests_in_progress() const {
      return total_requests_in_progress_.Load(MemoryOrder::RELAXED);
    }
    uint64_t total_error_requests() const {
      return total_error_requests_.Load(MemoryOrder::RELAXED);
    }
    uint64_t total_issued_requests() const {
      return total_issued_requests_.Load(MemoryOrder::RELAXED);
    }
    const LoadMetricList& load_metric_stats() const {
      return load_metric_stats_;
    }

   private:
    Atomic<uint64_t> total_successful_requests_{0};
    Atomic<uint64_t> total_requests_in_progress_{0};
    // Requests that were issued (not dropped) but failed.
    Atomic<uint64_t> total_error_requests_{0};
    Atomic<uint64_t> total_issued_requests_{0};
    LoadMetricList load_metric_stats_;
    bool dying_ = false;
  };

  using LocalityStatsMap =
      Map<RefCountedPtr<XdsLocalityName>, LocalityStats, XdsLocalityName::Less>;
  using DroppedRequestsMap = Map<UniquePtr<char>, uint64_t, StringLess>;

  XdsLbClientStats() = default;
  XdsLbClientStats(XdsLbClientStats&& other) noexcept;

  // Returns a snapshot of this instance and reset all the accumulative
  // counters.
  XdsLbClientStats Harvest();
  // TODO(juanlishen): Change this to const method when const_iterator is added
  // to Map<>.
  bool IsAllZero();

  void MaybeInitLastReportTime();
  LocalityStats* FindLocalityStats(
      const RefCountedPtr<XdsLocalityName>& locality_name);
  void PruneLocalityStats();
  void AddCallDropped(UniquePtr<char> category);

  // TODO(juanlishen): Change this to const method when const_iterator is added
  // to Map<>.
  LocalityStatsMap& upstream_locality_stats() {
    return upstream_locality_stats_;
  }
  uint64_t total_dropped_requests() const {
    return total_dropped_requests_.Load(MemoryOrder::RELAXED);
  }
  // TODO(juanlishen): Change this to const method when const_iterator is added
  // to Map<>.
  DroppedRequestsMap& dropped_requests() { return dropped_requests_; }
  grpc_millis load_report_interval() const { return load_report_interval_; }

 private:
  // The stats for each locality.
  LocalityStatsMap upstream_locality_stats_;
  Atomic<uint64_t> total_dropped_requests_{0};
  // Protects dropped_requests_. A mutex is necessary because the length of
  // DroppedRequestsList can be accessed by both the picker (from data plane
  // combiner) and the load reporting thread (from the control plane combiner).
  Mutex dropped_requests_mu_;
  DroppedRequestsMap dropped_requests_;
  // The actual load report interval.
  grpc_millis load_report_interval_;
  // The timestamp of last reporting. For the LB-policy-wide first report, the
  // last_report_time is the time we scheduled the first reporting timer.
  grpc_millis last_report_time_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_CLIENT_STATS_H \
        */
