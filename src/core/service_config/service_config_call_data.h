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

#ifndef GRPC_SRC_CORE_SERVICE_CONFIG_SERVICE_CONFIG_CALL_DATA_H
#define GRPC_SRC_CORE_SERVICE_CONFIG_SERVICE_CONFIG_CALL_DATA_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <memory>
#include <utility>

#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/chunked_vector.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_parser.h"

namespace grpc_core {

/// Stores the service config data associated with an individual call.
/// A pointer to this object is stored in the call_context
/// GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA element, so that filters can
/// easily access method and global parameters for the call.
///
/// Must be accessed when holding the call combiner (legacy filter) or from
/// inside the activity (promise-based filter).
class ServiceConfigCallData {
 public:
  class CallAttributeInterface {
   public:
    virtual ~CallAttributeInterface() = default;
    virtual UniqueTypeName type() const = 0;
  };

  ServiceConfigCallData(Arena* arena, grpc_call_context_element* call_context)
      : call_attributes_(arena) {
    call_context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value = this;
    call_context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].destroy = Destroy;
  }

  virtual ~ServiceConfigCallData() = default;

  void SetServiceConfig(
      RefCountedPtr<ServiceConfig> service_config,
      const ServiceConfigParser::ParsedConfigVector* method_configs) {
    service_config_ = std::move(service_config);
    method_configs_ = method_configs;
  }

  ServiceConfig* service_config() { return service_config_.get(); }

  ServiceConfigParser::ParsedConfig* GetMethodParsedConfig(size_t index) const {
    if (method_configs_ == nullptr) return nullptr;
    return (*method_configs_)[index].get();
  }

  ServiceConfigParser::ParsedConfig* GetGlobalParsedConfig(size_t index) const {
    if (service_config_ == nullptr) return nullptr;
    return service_config_->GetGlobalParsedConfig(index);
  }

  void SetCallAttribute(CallAttributeInterface* value) {
    // Overwrite existing entry if we already have one for this type.
    for (CallAttributeInterface*& attribute : call_attributes_) {
      if (value->type() == attribute->type()) {
        attribute = value;
        return;
      }
    }
    // Otherwise, add a new entry.
    call_attributes_.EmplaceBack(value);
  }

  template <typename A>
  A* GetCallAttribute() const {
    return static_cast<A*>(GetCallAttribute(A::TypeName()));
  }

  CallAttributeInterface* GetCallAttribute(UniqueTypeName type) const {
    for (CallAttributeInterface* attribute : call_attributes_) {
      if (attribute->type() == type) return attribute;
    }
    return nullptr;
  }

 private:
  static void Destroy(void* ptr) {
    auto* self = static_cast<ServiceConfigCallData*>(ptr);
    self->~ServiceConfigCallData();
  }

  RefCountedPtr<ServiceConfig> service_config_;
  const ServiceConfigParser::ParsedConfigVector* method_configs_ = nullptr;
  ChunkedVector<CallAttributeInterface*, 4> call_attributes_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_SERVICE_CONFIG_SERVICE_CONFIG_CALL_DATA_H
