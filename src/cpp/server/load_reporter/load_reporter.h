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

#include <grpc/impl/codegen/port_platform.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <vector>

#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/config.h>

// TODO(juanlishen): Difference?
//#include
//"src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h"

#include "src/cpp/server/load_reporter/load_data_store.h"
#include "src/proto/grpc/lb/v1/load_reporter.grpc.pb.h"

using grpc::lb::v1::CallMetricData;
using grpc::lb::v1::Duration;
using grpc::lb::v1::InitialLoadReportRequest;
using grpc::lb::v1::InitialLoadReportResponse;
using grpc::lb::v1::Load;
using grpc::lb::v1::LoadBalancingFeedback;
using grpc::lb::v1::LoadReportRequest;
using grpc::lb::v1::LoadReportResponse;
using grpc::lb::v1::OrphanedLoadIdentifier;

namespace grpc {

class CensusViewProvider {
 public:
  virtual bool FetchData() = 0;
};

class CensusViewProviderDefaultImpl : public CensusViewProvider {
 public:
  bool FetchData() override;
};

class CpuStatsProvider {
 public:
  // Gets the cumulative used CPU and total CPU resource.
  virtual std::pair<double, double> GetCpuStats() = 0;
};

// The default implementation reads CPU busy and idle jiffies from the
// /proc/stats to calculate CPU utilization.
class CpuStatsProviderDefaultImpl : public CpuStatsProvider {
 public:
  std::pair<double, double> GetCpuStats() override;
};

// Thread-safe.
class LoadReporter {
 public:
  // TODO(juanlishen): allow config for providers.
  // Takes ownership of the CensusViewProvider and CpuStatsProvider.
  LoadReporter(uint64_t feedback_sample_window_seconds,
               CensusViewProvider* census_view_provider,
               CpuStatsProvider* cpu_stats_provider)
      : feedback_sample_window_seconds_(feedback_sample_window_seconds),
        census_view_provider_(census_view_provider),
        cpu_stats_provider_(cpu_stats_provider) {
    AppendNewFeedbackRecord(0, 0);
  }

  // Fetches the latest data from Census and merge it into the data store.
  // Also updates the LB feedback sliding window with a new sample.
  void FetchAndSample();

  // Generates a report for that host and balancer. The report contains all
  // the stats data accumulated between the last report (i.e., the last
  // consumption) and the last fetch (i.e., the last production).
  ::google::protobuf::RepeatedPtrField<Load> GenerateLoads(
      grpc::string hostname, grpc::string lb_id);

  // The feedback is calculated from the stats data recorded in the sliding
  // window.
  LoadBalancingFeedback GenerateLoadBalancingFeedback();

  void ReportStreamCreated(grpc::string hostname, grpc::string lb_id,
                           grpc::string load_key);

  void ReportStreamClosed(grpc::string hostname, grpc::string lb_id);

  grpc::string GenerateLbId();

 private:
  typedef struct LoadBalancingFeedbackRecord {
    std::chrono::system_clock::time_point end_time_;
    uint64_t rpcs_;
    uint64_t errors_;
    double cpu_usage_;
    double cpu_limit_;
  } LoadBalancingFeedbackRecord;

  bool IsRecordInWindow(const LoadBalancingFeedbackRecord& record,
                        std::chrono::system_clock::time_point now) {
    return record.end_time_ > now - feedback_sample_window_seconds_;
  }

  void SetFakeCpuUsagePerRpc(double seconds_per_rpc) {
    fake_cpu_usage_per_rpc_ = seconds_per_rpc;
  }

  void AppendNewFeedbackRecord(uint64_t rpcs, uint64_t errors);

  void AttachOrphanLoadId(Load* load,
                          const PerBalancerStore* per_balancer_store);

  std::atomic_int64_t next_lb_id_{0};
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
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_LOAD_REPORTER_H
