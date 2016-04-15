/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "test/cpp/qps/report.h"

#include <fstream>

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/type_resolver_util.h>

#include <grpc/support/log.h>
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/stats.h"

namespace grpc {
namespace testing {

static double WallTime(ClientStats s) { return s.time_elapsed(); }
static double SystemTime(ClientStats s) { return s.time_system(); }
static double UserTime(ClientStats s) { return s.time_user(); }
static double ServerWallTime(ServerStats s) { return s.time_elapsed(); }
static double ServerSystemTime(ServerStats s) { return s.time_system(); }
static double ServerUserTime(ServerStats s) { return s.time_user(); }
static int Cores(int n) { return n; }

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

void GprLogReporter::ReportQPS(const ScenarioResult& result) {
  Histogram histogram;
  histogram.MergeProto(result.latencies());
  gpr_log(GPR_INFO, "QPS: %.1f",
          histogram.Count() / average(result.client_stats(), WallTime));
}

void GprLogReporter::ReportQPSPerCore(const ScenarioResult& result) {
  Histogram histogram;
  histogram.MergeProto(result.latencies());
  auto qps = histogram.Count() / average(result.client_stats(), WallTime);

  gpr_log(GPR_INFO, "QPS: %.1f (%.1f/server core)", qps,
          qps / sum(result.server_cores(), Cores));
}

void GprLogReporter::ReportLatency(const ScenarioResult& result) {
  Histogram histogram;
  histogram.MergeProto(result.latencies());
  gpr_log(GPR_INFO,
          "Latencies (50/90/95/99/99.9%%-ile): %.1f/%.1f/%.1f/%.1f/%.1f us",
          histogram.Percentile(50) / 1000, histogram.Percentile(90) / 1000,
          histogram.Percentile(95) / 1000, histogram.Percentile(99) / 1000,
          histogram.Percentile(99.9) / 1000);
}

void GprLogReporter::ReportTimes(const ScenarioResult& result) {
  gpr_log(GPR_INFO, "Server system time: %.2f%%",
          100.0 * sum(result.server_stats(), ServerSystemTime) /
              sum(result.server_stats(), ServerWallTime));
  gpr_log(GPR_INFO, "Server user time:   %.2f%%",
          100.0 * sum(result.server_stats(), ServerUserTime) /
              sum(result.server_stats(), ServerWallTime));
  gpr_log(GPR_INFO, "Client system time: %.2f%%",
          100.0 * sum(result.client_stats(), SystemTime) /
              sum(result.client_stats(), WallTime));
  gpr_log(GPR_INFO, "Client user time:   %.2f%%",
          100.0 * sum(result.client_stats(), UserTime) /
              sum(result.client_stats(), WallTime));
}

void JsonReporter::ReportQPS(const ScenarioResult& result) {
  std::unique_ptr<google::protobuf::util::TypeResolver> type_resolver(
      google::protobuf::util::NewTypeResolverForDescriptorPool(
          "type.googleapis.com",
          google::protobuf::DescriptorPool::generated_pool()));
  grpc::string binary;
  grpc::string json_string;
  result.SerializeToString(&binary);
  auto status = BinaryToJsonString(type_resolver.get(),
      "type.googleapis.com/grpc.testing.ScenarioResult",
      binary, &json_string);
  GPR_ASSERT(status.ok());

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

}  // namespace testing
}  // namespace grpc
