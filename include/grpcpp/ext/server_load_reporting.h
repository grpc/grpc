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

namespace grpc {
namespace load_reporter {

// Enables the server load reporting feature, including registering the channel
// filter to collect the load data, and adding the server builder plugin to
// start the load reporting service. It should be called before the server
// builder starts building the server. And it should be called at most by one
// thread for once. This should only be called if the binary includes
// grpcpp_server_load_reporting build target as a dependency. At this moment,
// it can only be built with Bazel.
// TODO(juanlishen): Put this option into ServerBuilder when OpenCensus can be
// built with other building tools.
void EnableServerLoadReporting();

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
