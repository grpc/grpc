//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <grpc/grpc.h>
#include <grpcpp/ext/server_load_reporting.h>
#include <grpcpp/impl/server_builder_plugin.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/cpp/server/load_reporter/load_reporting_service_server_builder_plugin.h"

namespace grpc {
namespace load_reporter {
namespace experimental {

void LoadReportingServiceServerBuilderOption::UpdateArguments(
    grpc::ChannelArguments* args) {
  args->SetInt(GRPC_ARG_ENABLE_LOAD_REPORTING, true);
}

void LoadReportingServiceServerBuilderOption::UpdatePlugins(
    std::vector<std::unique_ptr<grpc::ServerBuilderPlugin>>* plugins) {
  plugins->emplace_back(
      new grpc::load_reporter::LoadReportingServiceServerBuilderPlugin());
}

}  // namespace experimental
}  // namespace load_reporter
}  // namespace grpc
