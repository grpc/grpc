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
#ifndef GRPC_TEST_CPP_METRICS_SERVER_H
#define GRPC_TEST_CPP_METRICS_SERVER_H

#include <map>
#include <mutex>

#include "src/proto/grpc/testing/metrics.grpc.pb.h"
#include "src/proto/grpc/testing/metrics.pb.h"

/*
 * This implements a Metrics server defined in
 * src/proto/grpc/testing/metrics.proto. Any
 * test service can use this to export Metrics (TODO (sreek): Only Gauges for
 * now).
 *
 * Example:
 *    MetricsServiceImpl metricsImpl;
 *    ..
 *    // Create QpsGauge(s). Note: QpsGauges can be created even after calling
 *    // 'StartServer'.
 *    QpsGauge qps_gauge1 = metricsImpl.CreateQpsGauge("foo", is_present);
 *    // qps_gauge1 can now be used anywhere in the program by first making a
 *    // one-time call qps_gauge1.Reset() and then calling qps_gauge1.Incr()
 *    // every time to increment a query counter
 *
 *    ...
 *    // Create the metrics server
 *    std::unique_ptr<grpc::Server> server = metricsImpl.StartServer(port);
 *    server->Wait(); // Note: This is blocking.
 */
namespace grpc {
namespace testing {

class QpsGauge {
 public:
  QpsGauge();

  // Initialize the internal timer and reset the query count to 0
  void Reset();

  // Increment the query count by 1
  void Incr();

  // Return the current qps (i.e query count divided by the time since this
  // QpsGauge object created (or Reset() was called))
  long Get();

 private:
  gpr_timespec start_time_;
  long num_queries_;
  std::mutex num_queries_mu_;
};

class MetricsServiceImpl final : public MetricsService::Service {
 public:
  grpc::Status GetAllGauges(ServerContext* context, const EmptyMessage* request,
                            ServerWriter<GaugeResponse>* writer) override;

  grpc::Status GetGauge(ServerContext* context, const GaugeRequest* request,
                        GaugeResponse* response) override;

  // Create a QpsGauge with name 'name'. is_present is set to true if the Gauge
  // is already present in the map.
  // NOTE: CreateQpsGauge can be called anytime (i.e before or after calling
  // StartServer).
  std::shared_ptr<QpsGauge> CreateQpsGauge(const grpc::string& name,
                                           bool* already_present);

  std::unique_ptr<grpc::Server> StartServer(int port);

 private:
  std::map<string, std::shared_ptr<QpsGauge>> qps_gauges_;
  std::mutex mu_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_METRICS_SERVER_H
