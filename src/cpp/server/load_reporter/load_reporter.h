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

#ifndef GRPC_SRC_CPP_SERVER_LOAD_REPORTER_LOAD_REPORTER_H
#define GRPC_SRC_CPP_SERVER_LOAD_REPORTER_LOAD_REPORTER_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <vector>

#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/config.h>

#include "src/cpp/server/load_reporter/load_data_store.h"
#include "src/proto/grpc/lb/v1/load_reporter.grpc.pb.h"

#include "opencensus/stats/stats.h"

namespace grpc {
namespace load_reporter {

// The interface to get the Census stats. Abstracted for mocking.
class CensusViewProvider {
 public:
  using ViewDataMap =
      std::unordered_map<grpc::string, ::opencensus::stats::ViewData>;
  using ViewDescriptorMap =
      std::unordered_map<grpc::string, ::opencensus::stats::ViewDescriptor>;

  CensusViewProvider();
  virtual ~CensusViewProvider() = default;

  // Fetches the view data accumulated since last fetching, and returns it as a
  // map from the view name to the view data.
  virtual ViewDataMap FetchViewData() = 0;

  const ::opencensus::stats::ViewDescriptor& FindViewDescriptor(
      const grpc::string& view_name) {
    auto it = view_descriptor_map_.find(view_name);
    GPR_ASSERT(it != view_descriptor_map_.end());
    return it->second;
  }

  // Get a row with the input tag values from the view data. Only used when we
  // know that row must exist because we have seen a row with the same tag
  // values in a related view data. Several ViewData's are considered related if
  // their views are based on the measures that are always recorded at the same
  // time.
  double static GetRelatedViewDataRowDouble(
      ViewDataMap& view_data_map, const char* view_name, size_t view_name_len,
      const std::vector<grpc::string>& tag_values);

 protected:
  // Tag keys.
  ::opencensus::stats::TagKey tag_key_token_;
  ::opencensus::stats::TagKey tag_key_host_;
  ::opencensus::stats::TagKey tag_key_user_id_;
  ::opencensus::stats::TagKey tag_key_status_;
  ::opencensus::stats::TagKey tag_key_metric_name_;

  ViewDescriptorMap view_descriptor_map_;
};

// The default implementation fetches the real stats from Census.
class CensusViewProviderDefaultImpl : public CensusViewProvider {
 public:
  CensusViewProviderDefaultImpl();

  ViewDataMap FetchViewData() override;

 private:
  std::unordered_map<grpc::string, ::opencensus::stats::View> view_map_;
};

// The interface to get the CPU stats. Abstracted for mocking.
class CpuStatsProvider {
 public:
  using CpuStatsSample = std::pair<uint64_t, uint64_t>;
  virtual ~CpuStatsProvider() = default;

  // Gets the cumulative used CPU and total CPU resource.
  virtual std::pair<uint64_t, uint64_t> GetCpuStats() = 0;
};

// The default implementation reads CPU busy and idle jiffies from the
// system to calculate CPU utilization.
class CpuStatsProviderDefaultImpl : public CpuStatsProvider {
 public:
  CpuStatsSample GetCpuStats() override;
};

// A thread-safe class that maintains all the load data and load reporting
// streams.
class LoadReporter {
 public:
  enum ResourceType { START_CALL, END_CALL, OTHER_METRIC };

  // TODO(juanlishen): allow config for providers.
  LoadReporter(uint32_t feedback_sample_window_seconds,
               std::unique_ptr<CensusViewProvider> census_view_provider,
               std::unique_ptr<CpuStatsProvider> cpu_stats_provider)
      : feedback_sample_window_seconds_(feedback_sample_window_seconds),
        census_view_provider_(std::move(census_view_provider)),
        cpu_stats_provider_(std::move(cpu_stats_provider)) {
    AppendNewFeedbackRecord(0, 0);
  }

  // Fetches the latest data from Census and merge it into the data store.
  // Also updates the LB feedback sliding window with a new sample.
  void FetchAndSample();

  // Generates a report for that host and balancer. The report contains
  // all the stats data accumulated between the last report (i.e., the last
  // consumption) and the last fetch (i.e., the last production).
  ::google::protobuf::RepeatedPtrField<::grpc::lb::v1::Load> GenerateLoads(
      const grpc::string& hostname, const grpc::string& lb_id);

  // The feedback is calculated from the stats data recorded in the sliding
  // window. Outdated records are discarded.
  ::grpc::lb::v1::LoadBalancingFeedback GenerateLoadBalancingFeedback();

  // Wrapper around LoadDataStore::ReportStreamCreated.
  void ReportStreamCreated(const grpc::string& hostname,
                           const grpc::string& lb_id,
                           const grpc::string& load_key);

  // Wrapper around LoadDataStore::ReportStreamClosed.
  void ReportStreamClosed(const grpc::string& hostname,
                          const grpc::string& lb_id);

  // Generates a unique LB ID.
  grpc::string GenerateLbId();

  // Accessors for testing.
  CensusViewProvider* census_view_provider() {
    return census_view_provider_.get();
  }
  CpuStatsProvider* cpu_stats_provider() { return cpu_stats_provider_.get(); }

 private:
  struct LoadBalancingFeedbackRecord {
    std::chrono::system_clock::time_point end_time;
    double rpcs;
    double errors;
    uint64_t cpu_usage;
    uint64_t cpu_limit;
  };

  bool IsRecordInWindow(const LoadBalancingFeedbackRecord& record,
                        std::chrono::system_clock::time_point now) {
    return record.end_time > now - feedback_sample_window_seconds_;
  }

  void SetFakeCpuUsagePerRpc(double seconds_per_rpc) {
    fake_cpu_usage_per_rpc_ = seconds_per_rpc;
  }

  void AppendNewFeedbackRecord(double rpcs, double errors);

  void AttachOrphanLoadId(::grpc::lb::v1::Load* load,
                          PerBalancerStore* per_balancer_store);

  std::atomic<int64_t> next_lb_id_{0};
  const std::chrono::seconds feedback_sample_window_seconds_;
  std::mutex feedback_mu_;
  std::unique_ptr<CensusViewProvider> census_view_provider_;
  std::unique_ptr<CpuStatsProvider> cpu_stats_provider_;
  std::deque<LoadBalancingFeedbackRecord> feedback_records_;
  // TODO(juanlishen): Lock in finer grain. Locking the whole store may be
  // too expensive.
  std::mutex store_mu_;
  LoadDataStore load_data_store_;
  double fake_cpu_usage_per_rpc_ = -1;
};

}  // namespace load_reporter
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_LOAD_REPORTER_H
