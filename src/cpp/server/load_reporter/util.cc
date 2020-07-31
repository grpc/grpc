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

#include <grpc/impl/codegen/port_platform.h>

#include <grpcpp/ext/server_load_reporting.h>

#include <cmath>

#include <grpc/support/log.h>

namespace grpc {
namespace load_reporter {
namespace experimental {

void AddLoadReportingCost(grpc::ServerContext* ctx,
                          const std::string& cost_name, double cost_value) {
  if (std::isnormal(cost_value)) {
    std::string buf;
    buf.resize(sizeof(cost_value) + cost_name.size());
    memcpy(&(*buf.begin()), &cost_value, sizeof(cost_value));
    memcpy(&(*buf.begin()) + sizeof(cost_value), cost_name.data(),
           cost_name.size());
    ctx->AddTrailingMetadata(GRPC_LB_COST_MD_KEY, buf);
  } else {
    gpr_log(GPR_ERROR, "Call metric value is not normal.");
  }
}

}  // namespace experimental
}  // namespace load_reporter
}  // namespace grpc
