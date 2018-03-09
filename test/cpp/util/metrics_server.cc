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
 *is % allowed in string
 */

#include "test/cpp/util/metrics_server.h"

#include <grpc/support/log.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

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
