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

#include <atomic>
#include <map>
#include <mutex>

#include "test/proto/metrics.grpc.pb.h"
#include "test/proto/metrics.pb.h"

/*
 * This implements a Metrics server defined in test/proto/metrics.proto. Any
 * test service can use this to export Metrics (TODO (sreek): Only Gauges for
 * now).
 *
 * Example:
 *    MetricsServiceImpl metricsImpl;
 *    ..
 *    // Create Gauge(s). Note: Gauges can be created even after calling
 *    // 'StartServer'.
 *    Gauge gauge1 = metricsImpl.CreateGauge("foo",is_present);
 *    // gauge1 can now be used anywhere in the program to set values.
 *    ...
 *    // Create the metrics server
 *    std::unique_ptr<grpc::Server> server = metricsImpl.StartServer(port);
 *    server->Wait(); // Note: This is blocking.
 */
namespace grpc {
namespace testing {

// TODO(sreek): Add support for other types of Gauges like Double, String in
// future
class Gauge {
 public:
  Gauge(long initial_val);
  void Set(long new_val);
  long Get();

 private:
  std::atomic_long val_;
};

class MetricsServiceImpl GRPC_FINAL : public MetricsService::Service {
 public:
  grpc::Status GetAllGauges(ServerContext* context, const EmptyMessage* request,
                            ServerWriter<GaugeResponse>* writer) GRPC_OVERRIDE;

  grpc::Status GetGauge(ServerContext* context, const GaugeRequest* request,
                        GaugeResponse* response) GRPC_OVERRIDE;

  // Create a Gauge with name 'name'. is_present is set to true if the Gauge
  // is already present in the map.
  // NOTE: CreateGauge can be called anytime (i.e before or after calling
  // StartServer).
  std::shared_ptr<Gauge> CreateGauge(const grpc::string& name,
                                     bool* already_present);

  std::unique_ptr<grpc::Server> StartServer(int port);

 private:
  std::map<string, std::shared_ptr<Gauge>> gauges_;
  std::mutex mu_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_METRICS_SERVER_H
