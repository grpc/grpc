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

#include <grpc/support/log.h>
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/parse_json.h"
#include "test/cpp/qps/stats.h"

#include <grpc++/client_context.h>
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

void GprLogReporter::ReportQPS(const ScenarioResult& result) {
  gpr_log(GPR_INFO, "QPS: %.1f", result.summary().qps());
  if (result.summary().failed_requests_per_second() > 0) {
    gpr_log(GPR_INFO, "failed requests/second: %.1f",
            result.summary().failed_requests_per_second());
    gpr_log(GPR_INFO, "successful requests/second: %.1f",
            result.summary().successful_requests_per_second());
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

}  // namespace testing
}  // namespace grpc
