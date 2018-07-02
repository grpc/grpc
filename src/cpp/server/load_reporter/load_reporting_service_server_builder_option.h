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

#ifndef GRPC_SRC_CPP_LOAD_REPORTING_SERVICE_SERVER_BUILDER_OPTION_H
#define GRPC_SRC_CPP_LOAD_REPORTING_SERVICE_SERVER_BUILDER_OPTION_H

#include <grpc/support/port_platform.h>

#include <grpcpp/impl/server_builder_option.h>

namespace grpc {
namespace load_reporter {

class LoadReportingServiceServerBuilderOption : public ServerBuilderOption {
 public:
  void UpdateArguments(::grpc::ChannelArguments* args) override;
  void UpdatePlugins(std::vector<std::unique_ptr<::grpc::ServerBuilderPlugin>>*
                         plugins) override;
};

}  // namespace load_reporter
}  // namespace grpc

#endif  // GRPC_SRC_CPP_LOAD_REPORTING_SERVICE_SERVER_BUILDER_OPTION_H
