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

#ifndef GRPC_SRC_CPP_SERVER_LOAD_REPORTER_CALL_METRICS_BUILDER_H
#define GRPC_SRC_CPP_SERVER_LOAD_REPORTER_CALL_METRICS_BUILDER_H

#include <grpc/impl/codegen/port_platform.h>

#include <vector>

#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/server_context.h>

namespace grpc {
namespace load_reporter {

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

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_CALL_METRICS_BUILDER_H
