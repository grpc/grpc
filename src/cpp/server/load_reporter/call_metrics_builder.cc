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

#include <grpcpp/ext/server_load_reporting.h>

#include <math.h>

#include <grpc/support/log.h>

namespace grpc {
namespace load_reporter {

CallMetricsBuilder& CallMetricsBuilder::AddMetric(const grpc::string& name,
                                                  double value) {
  if (std::isnormal(value)) {
    grpc::string buf;
    buf.resize(sizeof(value) + name.size());
    memcpy(&(*buf.begin()), &value, sizeof(value));
    memcpy(&(*buf.begin()) + sizeof(value), name.data(), name.size());
    serialized_metrics_.push_back(buf);
  } else {
    gpr_log(GPR_ERROR, "Call metric value is not normal.");
  }
  return *this;
}

void CallMetricsBuilder::ApplyTo(::grpc::ServerContext* ctx) {
  for (const auto& metric : serialized_metrics_) {
    ctx->AddTrailingMetadata(GRPC_LB_COST_MD_KEY, metric);
  }
  serialized_metrics_.clear();
}

}  // namespace load_reporter
}  // namespace grpc
