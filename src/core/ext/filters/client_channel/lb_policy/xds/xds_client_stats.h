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

#include <grpc/support/atm.h>

#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
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

 private:
  UniquePtr<char> region_;
  UniquePtr<char> zone_;
  UniquePtr<char> sub_zone_;
};

class XdsLbClientStats : public RefCounted<XdsLbClientStats> {
 public:
  struct LocalityStats {
    struct LoadMetric {
      LoadMetric Harvest();
      UniquePtr<char> metric_name;
      gpr_atm num_requests_finished_with_metric;
      // FIXME: this should be double.
      gpr_atm total_metric_value;
    };

    LocalityStats Harvest();

    void Kill() { dying = true; }
    void Revive() { dying = false; }
    bool IsSafeToDelete() { return dying && total_requests_in_progress == 0; }

    void AddCallStarted();
    void AddCallFinished(bool fail = false);

    static void Destroy(void* arg) {}

    gpr_atm total_successful_requests;
    gpr_atm total_requests_in_progress;
    // Requests that were issued (not dropped) but failed.
    gpr_atm total_error_requests;
    gpr_atm total_issued_requests;
    InlinedVector<LoadMetric, 1> load_metric_stats;
    // TODO(juanlishen): Add upstream_endpoint_stats when we add per-backend
    // load reporting.
    bool dying = false;
  };

  struct DroppedRequests {
    DroppedRequests() {}
    DroppedRequests(const char* category, gpr_atm dropped_count)
        : category(category), dropped_count(dropped_count) {}

    DroppedRequests Harvest();

    // FIXME: should be null terminated
    const char* category = nullptr;
    gpr_atm dropped_count = 0;
  };

  XdsLbClientStats() {}
  // FIXME
  // Copy ctor.
  XdsLbClientStats(const XdsLbClientStats& other) {}

  XdsLbClientStats Harvest();

  void SetLastReportTimeToNow();

  LocalityStats* FindLocalityStats(
      const RefCountedPtr<XdsLocalityName>& locality_name);

  void PruneLocalityStats();

  void AddCallDropped(const char* category);

 private:
  friend grpc_slice XdsLrsRequestCreateAndEncode(
      XdsLbClientStats* client_stats);

  // The name of the server.
  const char* cluster_name;
  // The stats for each locality.
  Map<RefCountedPtr<XdsLocalityName>, LocalityStats, XdsLocalityName::Less>
      upstream_locality_stats;
  gpr_atm total_dropped_requests;
  InlinedVector<DroppedRequests, 2> dropped_requests;
  // The actual load report interval.
  grpc_millis load_report_interval;
  grpc_millis last_report_time;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_CLIENT_STATS_H \
        */
