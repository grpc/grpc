/*
 *
 * Copyright 2019 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpcpp/support/validate_service_config.h>

#include "src/core/ext/filters/client_channel/service_config.h"

namespace grpc {
namespace experimental {
std::string ValidateServiceConfigJSON(const std::string& service_config_json) {
  grpc_init();
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::ServiceConfig::Create(/*args=*/nullptr,
                                   service_config_json.c_str(), &error);
  std::string return_value;
  if (error != GRPC_ERROR_NONE) {
    return_value = grpc_error_std_string(error);
    GRPC_ERROR_UNREF(error);
  }
  grpc_shutdown();
  return return_value;
}
}  // namespace experimental
}  // namespace grpc
