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
 *is % allowed in string
 */

#include <memory>
#include <string>

#include <gflags/gflags.h>
#include <grpc++/grpc++.h>

#include "test/cpp/util/metrics_server.h"
#include "test/cpp/util/test_config.h"
#include "test/proto/metrics.grpc.pb.h"
#include "test/proto/metrics.pb.h"

DEFINE_string(metrics_server_address, "",
              "The metrics server addresses in the fomrat <hostname>:<port>");

using grpc::testing::EmptyMessage;
using grpc::testing::GaugeResponse;
using grpc::testing::MetricsService;
using grpc::testing::MetricsServiceImpl;

void PrintMetrics(grpc::string& server_address) {
  gpr_log(GPR_INFO, "creating a channel to %s", server_address.c_str());
  std::shared_ptr<grpc::Channel> channel(
      grpc::CreateChannel(server_address, grpc::InsecureCredentials()));

  std::unique_ptr<MetricsService::Stub> stub(MetricsService::NewStub(channel));

  grpc::ClientContext context;
  EmptyMessage message;

  std::unique_ptr<grpc::ClientReader<GaugeResponse>> reader(
      stub->GetAllGauges(&context, message));

  GaugeResponse gauge_response;
  long overall_qps = 0;
  int idx = 0;
  while (reader->Read(&gauge_response)) {
    if (gauge_response.value_case() == GaugeResponse::kLongValue) {
      gpr_log(GPR_INFO, "Gauge: %d (%s: %ld)", ++idx,
              gauge_response.name().c_str(), gauge_response.long_value());
      overall_qps += gauge_response.long_value();
    } else {
      gpr_log(GPR_INFO, "Gauge %s is not a long value", gauge_response.name().c_str());
    }
  }

  gpr_log(GPR_INFO, "OVERALL: %ld", overall_qps);

  const grpc::Status status = reader->Finish();
  if (!status.ok()) {
    gpr_log(GPR_ERROR, "Error in getting metrics from the client");
  }
}

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  // Make sure server_addresses flag is not empty
  if (FLAGS_metrics_server_address.empty()) {
    gpr_log(
        GPR_ERROR,
        "Cannot connect to the Metrics server. Please pass the address of the"
        "metrics server to connect to via the 'metrics_server_address' flag");
    return 1;
  }

  PrintMetrics(FLAGS_metrics_server_address);

  return 0;
}
