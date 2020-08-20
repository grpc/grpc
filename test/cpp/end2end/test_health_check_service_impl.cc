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

#include "test/cpp/end2end/test_health_check_service_impl.h"

#include <grpc/grpc.h>

using grpc::health::v1::HealthCheckRequest;
using grpc::health::v1::HealthCheckResponse;

namespace grpc {
namespace testing {

Status HealthCheckServiceImpl::Check(ServerContext* /*context*/,
                                     const HealthCheckRequest* request,
                                     HealthCheckResponse* response) {
  std::lock_guard<std::mutex> lock(mu_);
  auto iter = status_map_.find(request->service());
  if (iter == status_map_.end()) {
    return Status(StatusCode::NOT_FOUND, "");
  }
  response->set_status(iter->second);
  return Status::OK;
}

Status HealthCheckServiceImpl::Watch(
    ServerContext* context, const HealthCheckRequest* request,
    ::grpc::ServerWriter<HealthCheckResponse>* writer) {
  auto last_state = HealthCheckResponse::UNKNOWN;
  while (!context->IsCancelled()) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      HealthCheckResponse response;
      auto iter = status_map_.find(request->service());
      if (iter == status_map_.end()) {
        response.set_status(response.SERVICE_UNKNOWN);
      } else {
        response.set_status(iter->second);
      }
      if (response.status() != last_state) {
        writer->Write(response, ::grpc::WriteOptions());
        last_state = response.status();
      }
    }
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                 gpr_time_from_millis(1000, GPR_TIMESPAN)));
  }
  return Status::OK;
}

void HealthCheckServiceImpl::SetStatus(
    const std::string& service_name,
    HealthCheckResponse::ServingStatus status) {
  std::lock_guard<std::mutex> lock(mu_);
  if (shutdown_) {
    status = HealthCheckResponse::NOT_SERVING;
  }
  status_map_[service_name] = status;
}

void HealthCheckServiceImpl::SetAll(HealthCheckResponse::ServingStatus status) {
  std::lock_guard<std::mutex> lock(mu_);
  if (shutdown_) {
    return;
  }
  for (auto iter = status_map_.begin(); iter != status_map_.end(); ++iter) {
    iter->second = status;
  }
}

void HealthCheckServiceImpl::Shutdown() {
  std::lock_guard<std::mutex> lock(mu_);
  if (shutdown_) {
    return;
  }
  shutdown_ = true;
  for (auto iter = status_map_.begin(); iter != status_map_.end(); ++iter) {
    iter->second = HealthCheckResponse::NOT_SERVING;
  }
}

}  // namespace testing
}  // namespace grpc
