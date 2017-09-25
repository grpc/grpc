/*
 *
 * Copyright 2015 gRPC authors.
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

#include "test/cpp/qps/report.h"

#include <fstream>

#include <grpc/support/log.h>
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/parse_json.h"
#include "test/cpp/qps/stats.h"

#include <grpc++/client_context.h>
#include "src/cpp/util/core_stats.h"
#include "src/proto/grpc/testing/services.grpc.pb.h"

namespace grpc {
namespace testing {

void CompositeReporter::add(std::unique_ptr<Reporter> reporter) {
  reporters_.emplace_back(std::move(reporter));
}

void CompositeReporter::ReportQPS(const ScenarioResult& result) {
  for (size_t i = 0; i < reporters_.size(); ++i) {
    reporters_[i]->ReportQPS(result);
  }
}

void CompositeReporter::ReportQPSPerCore(const ScenarioResult& result) {
  for (size_t i = 0; i < reporters_.size(); ++i) {
    reporters_[i]->ReportQPSPerCore(result);
  }
}

void CompositeReporter::ReportLatency(const ScenarioResult& result) {
  for (size_t i = 0; i < reporters_.size(); ++i) {
    reporters_[i]->ReportLatency(result);
  }
}

void CompositeReporter::ReportTimes(const ScenarioResult& result) {
  for (size_t i = 0; i < reporters_.size(); ++i) {
    reporters_[i]->ReportTimes(result);
  }
}

void CompositeReporter::ReportCpuUsage(const ScenarioResult& result) {
  for (size_t i = 0; i < reporters_.size(); ++i) {
    reporters_[i]->ReportCpuUsage(result);
  }
}

void CompositeReporter::ReportPollCount(const ScenarioResult& result) {
  for (size_t i = 0; i < reporters_.size(); ++i) {
    reporters_[i]->ReportPollCount(result);
  }
}

void CompositeReporter::ReportQueriesPerCpuSec(const ScenarioResult& result) {
  for (size_t i = 0; i < reporters_.size(); ++i) {
    reporters_[i]->ReportQueriesPerCpuSec(result);
  }
}

void GprLogReporter::ReportQPS(const ScenarioResult& result) {
  gpr_log(GPR_INFO, "QPS: %.1f", result.summary().qps());
  if (result.summary().failed_requests_per_second() > 0) {
    gpr_log(GPR_INFO, "failed requests/second: %.1f",
            result.summary().failed_requests_per_second());
    gpr_log(GPR_INFO, "successful requests/second: %.1f",
            result.summary().successful_requests_per_second());
  }
  for (int i = 0; i < result.client_stats_size(); i++) {
    if (result.client_stats(i).has_core_stats()) {
      ReportCoreStats("CLIENT", i, result.client_stats(i).core_stats());
    }
  }
  for (int i = 0; i < result.server_stats_size(); i++) {
    if (result.server_stats(i).has_core_stats()) {
      ReportCoreStats("SERVER", i, result.server_stats(i).core_stats());
    }
  }
}

void GprLogReporter::ReportCoreStats(const char* name, int idx,
                                     const grpc::core::Stats& stats) {
  grpc_stats_data data;
  ProtoToCoreStats(stats, &data);
  for (int i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
    gpr_log(GPR_DEBUG, "%s[%d].%s = %" PRIdPTR, name, idx,
            grpc_stats_counter_name[i], data.counters[i]);
  }
  for (int i = 0; i < GRPC_STATS_HISTOGRAM_COUNT; i++) {
    gpr_log(GPR_DEBUG, "%s[%d].%s = %.1lf/%.1lf/%.1lf (50/95/99%%-ile)", name,
            idx, grpc_stats_histogram_name[i],
            grpc_stats_histo_percentile(&data, (grpc_stats_histograms)i, 50),
            grpc_stats_histo_percentile(&data, (grpc_stats_histograms)i, 95),
            grpc_stats_histo_percentile(&data, (grpc_stats_histograms)i, 99));
  }
}

void GprLogReporter::ReportQPSPerCore(const ScenarioResult& result) {
  gpr_log(GPR_INFO, "QPS: %.1f (%.1f/server core)", result.summary().qps(),
          result.summary().qps_per_server_core());
}

void GprLogReporter::ReportLatency(const ScenarioResult& result) {
  gpr_log(GPR_INFO,
          "Latencies (50/90/95/99/99.9%%-ile): %.1f/%.1f/%.1f/%.1f/%.1f us",
          result.summary().latency_50() / 1000,
          result.summary().latency_90() / 1000,
          result.summary().latency_95() / 1000,
          result.summary().latency_99() / 1000,
          result.summary().latency_999() / 1000);
}

void GprLogReporter::ReportTimes(const ScenarioResult& result) {
  gpr_log(GPR_INFO, "Server system time: %.2f%%",
          result.summary().server_system_time());
  gpr_log(GPR_INFO, "Server user time:   %.2f%%",
          result.summary().server_user_time());
  gpr_log(GPR_INFO, "Client system time: %.2f%%",
          result.summary().client_system_time());
  gpr_log(GPR_INFO, "Client user time:   %.2f%%",
          result.summary().client_user_time());
}

void GprLogReporter::ReportCpuUsage(const ScenarioResult& result) {
  gpr_log(GPR_INFO, "Server CPU usage: %.2f%%",
          result.summary().server_cpu_usage());
}

void GprLogReporter::ReportPollCount(const ScenarioResult& result) {
  gpr_log(GPR_INFO, "Client Polls per Request: %.2f",
          result.summary().client_polls_per_request());
  gpr_log(GPR_INFO, "Server Polls per Request: %.2f",
          result.summary().server_polls_per_request());
}

void GprLogReporter::ReportQueriesPerCpuSec(const ScenarioResult& result) {
  gpr_log(GPR_INFO, "Server Queries/CPU-sec: %.2f",
          result.summary().server_queries_per_cpu_sec());
  gpr_log(GPR_INFO, "Client Queries/CPU-sec: %.2f",
          result.summary().client_queries_per_cpu_sec());
}

void JsonReporter::ReportQPS(const ScenarioResult& result) {
  grpc::string json_string =
      SerializeJson(result, "type.googleapis.com/grpc.testing.ScenarioResult");
  std::ofstream output_file(report_file_);
  output_file << json_string;
  output_file.close();
}

void JsonReporter::ReportQPSPerCore(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void JsonReporter::ReportLatency(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void JsonReporter::ReportTimes(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void JsonReporter::ReportCpuUsage(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void JsonReporter::ReportPollCount(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void JsonReporter::ReportQueriesPerCpuSec(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void RpcReporter::ReportQPS(const ScenarioResult& result) {
  grpc::ClientContext context;
  grpc::Status status;
  Void dummy;

  gpr_log(GPR_INFO, "RPC reporter sending scenario result to server");
  status = stub_->ReportScenario(&context, result, &dummy);

  if (status.ok()) {
    gpr_log(GPR_INFO, "RpcReporter report RPC success!");
  } else {
    gpr_log(GPR_ERROR, "RpcReporter report RPC: code: %d. message: %s",
            status.error_code(), status.error_message().c_str());
  }
}

void RpcReporter::ReportQPSPerCore(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void RpcReporter::ReportLatency(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void RpcReporter::ReportTimes(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void RpcReporter::ReportCpuUsage(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void RpcReporter::ReportPollCount(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

void RpcReporter::ReportQueriesPerCpuSec(const ScenarioResult& result) {
  // NOP - all reporting is handled by ReportQPS.
}

}  // namespace testing
}  // namespace grpc
