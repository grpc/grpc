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

#ifndef GRPC_CORE_LIB_CHANNEL_SERVER_CONFIG_CALL_DATA_H
#define GRPC_CORE_LIB_CHANNEL_SERVER_CONFIG_CALL_DATA_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/service_config.h"
#include "src/core/lib/channel/service_config_parser.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

class ServerConfigCallData {
  // Similar to ServiceConfigCallData on the client-side. This will be used by
  // the xds filters in the server-side filter stack. In the future, this might
  // be expanded so as to be able to configure other filters too similar to
  // clients.
 public:
  ServerConfigCallData(
      RefCountedPtr<ServiceConfig> service_config,
      const ServiceConfigParser::ParsedConfigVector* method_configs,
      grpc_call_context_element* call_context)
      : service_config_(std::move(service_config)),
        method_configs_(method_configs) {
    call_context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value = this;
    call_context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].destroy = Destroy;
  }

  ServiceConfig* service_config() { return service_config_.get(); }

  ServiceConfigParser::ParsedConfig* GetMethodParsedConfig(size_t index) const {
    return method_configs_ != nullptr ? (*method_configs_)[index].get()
                                      : nullptr;
  }

  ServiceConfigParser::ParsedConfig* GetGlobalParsedConfig(size_t index) const {
    return service_config_->GetGlobalParsedConfig(index);
  }

 private:
  static void Destroy(void* ptr) {
    ServerConfigCallData* self = static_cast<ServerConfigCallData*>(ptr);
    self->~ServerConfigCallData();
  }

  RefCountedPtr<ServiceConfig> service_config_;
  const ServiceConfigParser::ParsedConfigVector* method_configs_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_SERVER_CONFIG_CALL_DATA_H */
