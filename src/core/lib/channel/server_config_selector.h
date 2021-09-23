//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CHANNEL_SERVER_CONFIG_SELECTOR_H
#define GRPC_CORE_LIB_CHANNEL_SERVER_CONFIG_SELECTOR_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/service_config.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// Similar to ConfigSelector on the client-side. Invoked by the config selector
// filter per call.
class ServerConfigSelector {
 public:
  struct CallConfig {
    grpc_error_handle error = GRPC_ERROR_NONE;
    const ServiceConfigParser::ParsedConfigVector* method_configs = nullptr;
    RefCountedPtr<ServiceConfig> service_config;
  };
  virtual CallConfig GetCallConfig(grpc_metadata_batch* metadata) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_CHANNEL_SERVER_CONFIG_SELECTOR_H
