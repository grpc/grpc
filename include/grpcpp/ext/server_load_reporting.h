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

#ifndef GRPCPP_EXT_SERVER_LOAD_REPORTING_H
#define GRPCPP_EXT_SERVER_LOAD_REPORTING_H

#include <grpc/support/port_platform.h>

#include <vector>

#include <grpc/server_load_reporting.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/server_context.h>
#include <grpcpp/impl/server_builder_option.h>

namespace grpc {
namespace load_reporter {

// The ServerBuilderOption to enable server-side load reporting feature. To
// enable the feature, please make sure the binary builds with the
// grpcpp_server_load_reporting library and set this option to the
// ServerBuilder.
class LoadReportingServiceServerBuilderOption : public ServerBuilderOption {
 public:
  void UpdateArguments(::grpc::ChannelArguments* args) override;
  void UpdatePlugins(std::vector<std::unique_ptr<::grpc::ServerBuilderPlugin>>*
                         plugins) override;
};

// A helper class to build call metrics to apply to the server context.
class CallMetricsBuilder {
 public:
  // Adds a call metric entry to the builder in its serialized format.
  CallMetricsBuilder& AddMetric(const grpc::string& name, double value);

  // Applies the saved call metrics to the server context, then clears them in
  // the class.
  void ApplyTo(::grpc::ServerContext* ctx);

 private:
  std::vector<grpc::string> serialized_metrics_;
};

}  // namespace load_reporter
}  // namespace grpc

#endif  // GRPCPP_EXT_SERVER_LOAD_REPORTING_H
