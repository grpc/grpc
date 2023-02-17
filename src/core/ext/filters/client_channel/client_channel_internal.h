//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_INTERNAL_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/strings/string_view.h"

#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/service_config/service_config_parser.h"

//
// This file contains internal interfaces used to allow various plugins
// (filters, LB policies, etc) to access internal data provided by the
// ClientChannel that is not normally accessible via external APIs.
//

namespace grpc_core {

// Internal type for LB call state interface.  Provides an interface for
// LB policies to access internal call attributes.
class ClientChannelLbCallState : public LoadBalancingPolicy::CallState {
 public:
  virtual absl::string_view GetCallAttribute(UniqueTypeName type) = 0;
};

// Internal type for ServiceConfigCallData.  Provides access to the
// CallDispatchController.
class ClientChannelServiceConfigCallData : public ServiceConfigCallData {
 public:
  ClientChannelServiceConfigCallData(
      RefCountedPtr<ServiceConfig> service_config,
      const ServiceConfigParser::ParsedConfigVector* method_configs,
      ServiceConfigCallData::CallAttributes call_attributes,
      ConfigSelector::CallDispatchController* call_dispatch_controller,
      grpc_call_context_element* call_context)
      : ServiceConfigCallData(std::move(service_config), method_configs,
                              std::move(call_attributes)),
        call_dispatch_controller_(call_dispatch_controller) {
    call_context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value = this;
    call_context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].destroy = Destroy;
  }

  ConfigSelector::CallDispatchController* call_dispatch_controller() {
    return &call_dispatch_controller_;
  }

 private:
  // A wrapper for the CallDispatchController returned by the ConfigSelector.
  // Handles the case where the ConfigSelector doees not return any
  // CallDispatchController.
  // Also ensures that we call Commit() at most once, which allows the
  // client channel code to call Commit() when the call is complete in case
  // it wasn't called earlier, without needing to know whether or not it was.
  class CallDispatchControllerWrapper
      : public ConfigSelector::CallDispatchController {
   public:
    explicit CallDispatchControllerWrapper(
        ConfigSelector::CallDispatchController* call_dispatch_controller)
        : call_dispatch_controller_(call_dispatch_controller) {}

    bool ShouldRetry() override {
      if (call_dispatch_controller_ != nullptr) {
        return call_dispatch_controller_->ShouldRetry();
      }
      return true;
    }

    void Commit() override {
      if (call_dispatch_controller_ != nullptr && !commit_called_) {
        call_dispatch_controller_->Commit();
        commit_called_ = true;
      }
    }

   private:
    ConfigSelector::CallDispatchController* call_dispatch_controller_;
    bool commit_called_ = false;
  };

  static void Destroy(void* ptr) {
    auto* self = static_cast<ClientChannelServiceConfigCallData*>(ptr);
    self->~ClientChannelServiceConfigCallData();
  }

  CallDispatchControllerWrapper call_dispatch_controller_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_INTERNAL_H
