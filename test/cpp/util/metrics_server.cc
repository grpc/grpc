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

#include "test/cpp/util/metrics_server.h"

#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/metrics.grpc.pb.h"
#include "src/proto/grpc/testing/metrics.pb.h"

namespace grpc {
namespace testing {

QpsGauge::QpsGauge()
    : start_time_(gpr_now(GPR_CLOCK_REALTIME)), num_queries_(0) {}

void QpsGauge::Reset() {
  std::lock_guard<std::mutex> lock(num_queries_mu_);
  num_queries_ = 0;
  start_time_ = gpr_now(GPR_CLOCK_REALTIME);
}

void QpsGauge::Incr() {
  std::lock_guard<std::mutex> lock(num_queries_mu_);
  num_queries_++;
}

long QpsGauge::Get() {
  std::lock_guard<std::mutex> lock(num_queries_mu_);
  gpr_timespec time_diff =
      gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), start_time_);
  long duration_secs = time_diff.tv_sec > 0 ? time_diff.tv_sec : 1;
  return num_queries_ / duration_secs;
}

grpc::Status MetricsServiceImpl::GetAllGauges(
    ServerContext* context, const EmptyMessage* request,
    ServerWriter<GaugeResponse>* writer) {
  gpr_log(GPR_DEBUG, "GetAllGauges called");

  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = qps_gauges_.begin(); it != qps_gauges_.end(); it++) {
    GaugeResponse resp;
    resp.set_name(it->first);                // Gauge name
    resp.set_long_value(it->second->Get());  // Gauge value
    writer->Write(resp);
  }

  return Status::OK;
}

grpc::Status MetricsServiceImpl::GetGauge(ServerContext* context,
                                          const GaugeRequest* request,
                                          GaugeResponse* response) {
  std::lock_guard<std::mutex> lock(mu_);

  const auto it = qps_gauges_.find(request->name());
  if (it != qps_gauges_.end()) {
    response->set_name(it->first);
    response->set_long_value(it->second->Get());
  }

  return Status::OK;
}

std::shared_ptr<QpsGauge> MetricsServiceImpl::CreateQpsGauge(
    const grpc::string& name, bool* already_present) {
  std::lock_guard<std::mutex> lock(mu_);

  std::shared_ptr<QpsGauge> qps_gauge(new QpsGauge());
  const auto p = qps_gauges_.insert(std::make_pair(name, qps_gauge));

  // p.first is an iterator pointing to <name, shared_ptr<QpsGauge>> pair.
  // p.second is a boolean which is set to 'true' if the QpsGauge is
  // successfully inserted in the guages_ map and 'false' if it is already
  // present in the map
  *already_present = !p.second;
  return p.first->second;
}

// Starts the metrics server and returns the grpc::Server instance. Call Wait()
// on the returned server instance.
std::unique_ptr<grpc::Server> MetricsServiceImpl::StartServer(int port) {
  gpr_log(GPR_INFO, "Building metrics server..");

  const grpc::string address = "0.0.0.0:" + grpc::to_string(port);

  ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Metrics server %s started. Ready to receive requests..",
          address.c_str());

  return server;
}

}  // namespace testing
}  // namespace grpc
