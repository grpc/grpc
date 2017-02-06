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
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/metrics.grpc.pb.h"
#include "src/proto/grpc/testing/metrics.pb.h"
#include "test/cpp/util/metrics_server.h"
#include "test/cpp/util/test_config.h"

int kDeadlineSecs = 10;

DEFINE_string(metrics_server_address, "localhost:8081",
              "The metrics server addresses in the fomrat <hostname>:<port>");
DEFINE_int32(deadline_secs, kDeadlineSecs,
             "The deadline (in seconds) for RCP call");
DEFINE_bool(total_only, false,
            "If true, this prints only the total value of all gauges");

using grpc::testing::EmptyMessage;
using grpc::testing::GaugeResponse;
using grpc::testing::MetricsService;
using grpc::testing::MetricsServiceImpl;

// Do not log anything
void BlackholeLogger(gpr_log_func_args* args) {}

// Prints the values of all Gauges (unless total_only is set to 'true' in which
// case this only prints the sum of all gauge values).
bool PrintMetrics(std::unique_ptr<MetricsService::Stub> stub, bool total_only,
                  int deadline_secs) {
  grpc::ClientContext context;
  EmptyMessage message;

  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(deadline_secs);

  context.set_deadline(deadline);

  std::unique_ptr<grpc::ClientReader<GaugeResponse>> reader(
      stub->GetAllGauges(&context, message));

  GaugeResponse gauge_response;
  long overall_qps = 0;
  while (reader->Read(&gauge_response)) {
    if (gauge_response.value_case() == GaugeResponse::kLongValue) {
      if (!total_only) {
        std::cout << gauge_response.name() << ": "
                  << gauge_response.long_value() << std::endl;
      }
      overall_qps += gauge_response.long_value();
    } else {
      std::cout << "Gauge '" << gauge_response.name() << "' is not long valued"
                << std::endl;
    }
  }

  std::cout << overall_qps << std::endl;

  const grpc::Status status = reader->Finish();
  if (!status.ok()) {
    std::cout << "Error in getting metrics from the client" << std::endl;
  }

  return status.ok();
}

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  // The output of metrics client is in some cases programatically parsed (for
  // example by the stress test framework). So, we do not want any of the log
  // from the grpc library appearing on stdout.
  gpr_set_log_function(BlackholeLogger);

  std::shared_ptr<grpc::Channel> channel(grpc::CreateChannel(
      FLAGS_metrics_server_address, grpc::InsecureChannelCredentials()));

  if (!PrintMetrics(MetricsService::NewStub(channel), FLAGS_total_only,
                    FLAGS_deadline_secs)) {
    return 1;
  }

  return 0;
}
