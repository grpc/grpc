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

#include <string>

#include <grpc/grpc.h>
#include <grpcpp/support/config.h>
#include <grpcpp/support/validate_service_config.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/service_config/service_config_impl.h"

namespace grpc {
namespace experimental {
std::string ValidateServiceConfigJSON(const std::string& service_config_json) {
  grpc_init();
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::ServiceConfigImpl::Create(grpc_core::ChannelArgs(),
                                       service_config_json.c_str(), &error);
  std::string return_value;
  if (!GRPC_ERROR_IS_NONE(error)) {
    return_value = grpc_error_std_string(error);
    GRPC_ERROR_UNREF(error);
  }
  grpc_shutdown();
  return return_value;
}
}  // namespace experimental
}  // namespace grpc
