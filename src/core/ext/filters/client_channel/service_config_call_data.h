//
// Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVICE_CONFIG_CALL_DATA_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVICE_CONFIG_CALL_DATA_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/service_config_parser.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

/// When a service config is applied to a call in the client_channel_filter,
/// we create an instance of this object on the arena.  A pointer to this
/// object is also stored in the call_context, so that future filters can
/// easily access method and global parameters for the call.
class ServiceConfigCallData {
 public:
  ServiceConfigCallData(
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
    ServiceConfigCallData* self = static_cast<ServiceConfigCallData*>(ptr);
    self->~ServiceConfigCallData();
  }

  RefCountedPtr<ServiceConfig> service_config_;
  const ServiceConfigParser::ParsedConfigVector* method_configs_ = nullptr;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVICE_CONFIG_CALL_DATA_H */
