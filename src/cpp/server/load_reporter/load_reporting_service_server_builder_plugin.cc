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

#include <grpc/support/port_platform.h>

#include "src/cpp/server/load_reporter/load_reporting_service_server_builder_plugin.h"

#include <grpcpp/impl/server_initializer.h>

namespace grpc {
namespace load_reporter {

bool LoadReportingServiceServerBuilderPlugin::has_sync_methods() const {
  if (service_ != nullptr) {
    return service_->has_synchronous_methods();
  }
  return false;
}

bool LoadReportingServiceServerBuilderPlugin::has_async_methods() const {
  if (service_ != nullptr) {
    return service_->has_async_methods();
  }
  return false;
}

void LoadReportingServiceServerBuilderPlugin::UpdateServerBuilder(
    grpc::ServerBuilder* builder) {
  auto cq = builder->AddCompletionQueue();
  service_ = std::make_shared<LoadReporterAsyncServiceImpl>(std::move(cq));
}

void LoadReportingServiceServerBuilderPlugin::InitServer(
    grpc::ServerInitializer* si) {
  si->RegisterService(service_);
}

void LoadReportingServiceServerBuilderPlugin::Finish(
    grpc::ServerInitializer* /*si*/) {
  service_->StartThread();
  service_.reset();
}

}  // namespace load_reporter
}  // namespace grpc
