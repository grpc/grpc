//
// Copyright 2020 gRPC authors.
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

// This filter reads GRPC_ARG_SERVICE_CONFIG and populates ServiceConfigCallData
// in the call context per call for direct channels.

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/service_config/service_config_impl.h"

namespace grpc_core {

namespace {

class ServiceConfigChannelArgChannelData {
 public:
  explicit ServiceConfigChannelArgChannelData(
      const grpc_channel_element_args* args) {
    const char* service_config_str = grpc_channel_args_find_string(
        args->channel_args, GRPC_ARG_SERVICE_CONFIG);
    if (service_config_str != nullptr) {
      grpc_error_handle service_config_error = GRPC_ERROR_NONE;
      auto service_config = ServiceConfigImpl::Create(
          args->channel_args, service_config_str, &service_config_error);
      if (service_config_error == GRPC_ERROR_NONE) {
        service_config_ = std::move(service_config);
      } else {
        gpr_log(GPR_ERROR, "%s",
                grpc_error_std_string(service_config_error).c_str());
      }
      GRPC_ERROR_UNREF(service_config_error);
    }
  }

  RefCountedPtr<ServiceConfig> service_config() const {
    return service_config_;
  }

 private:
  RefCountedPtr<ServiceConfig> service_config_;
};

class ServiceConfigChannelArgCallData {
 public:
  ServiceConfigChannelArgCallData(
      RefCountedPtr<ServiceConfig> service_config,
      const ServiceConfigParser::ParsedConfigVector* method_config,
      const grpc_call_element_args* args)
      : call_context_(args->context),
        service_config_call_data_(std::move(service_config), method_config,
                                  /*call_attributes=*/{}) {
    GPR_DEBUG_ASSERT(args->context != nullptr);
    // No need to set the destroy function, since it will be cleaned up
    // when this filter is destroyed in the filter stack.
    args->context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value =
        &service_config_call_data_;
  }

  ~ServiceConfigChannelArgCallData() {
    // Remove the entry from call context, just in case anyone above us
    // tries to look at it during call stack destruction.
    call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value = nullptr;
  }

 private:
  grpc_call_context_element* call_context_;
  ServiceConfigCallData service_config_call_data_;
};

grpc_error_handle ServiceConfigChannelArgInitCallElem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  auto* chand =
      static_cast<ServiceConfigChannelArgChannelData*>(elem->channel_data);
  auto* calld = static_cast<ServiceConfigChannelArgCallData*>(elem->call_data);
  RefCountedPtr<ServiceConfig> service_config = chand->service_config();
  const ServiceConfigParser::ParsedConfigVector* method_config = nullptr;
  if (service_config != nullptr) {
    method_config = service_config->GetMethodParsedConfigVector(args->path);
  }
  new (calld) ServiceConfigChannelArgCallData(std::move(service_config),
                                              method_config, args);
  return GRPC_ERROR_NONE;
}

void ServiceConfigChannelArgDestroyCallElem(
    grpc_call_element* elem, const grpc_call_final_info* /* final_info */,
    grpc_closure* /* then_schedule_closure */) {
  ServiceConfigChannelArgCallData* calld =
      static_cast<ServiceConfigChannelArgCallData*>(elem->call_data);
  calld->~ServiceConfigChannelArgCallData();
}

grpc_error_handle ServiceConfigChannelArgInitChannelElem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  ServiceConfigChannelArgChannelData* chand =
      static_cast<ServiceConfigChannelArgChannelData*>(elem->channel_data);
  new (chand) ServiceConfigChannelArgChannelData(args);
  return GRPC_ERROR_NONE;
}

void ServiceConfigChannelArgDestroyChannelElem(grpc_channel_element* elem) {
  ServiceConfigChannelArgChannelData* chand =
      static_cast<ServiceConfigChannelArgChannelData*>(elem->channel_data);
  chand->~ServiceConfigChannelArgChannelData();
}

const grpc_channel_filter ServiceConfigChannelArgFilter = {
    grpc_call_next_op,
    nullptr,
    grpc_channel_next_op,
    sizeof(ServiceConfigChannelArgCallData),
    ServiceConfigChannelArgInitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    ServiceConfigChannelArgDestroyCallElem,
    sizeof(ServiceConfigChannelArgChannelData),
    ServiceConfigChannelArgInitChannelElem,
    ServiceConfigChannelArgDestroyChannelElem,
    grpc_channel_next_get_info,
    "service_config_channel_arg"};

}  // namespace

void RegisterServiceConfigChannelArgFilter(
    CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        const grpc_channel_args* channel_args = builder->channel_args();
        if (grpc_channel_args_want_minimal_stack(channel_args) ||
            grpc_channel_args_find_string(channel_args,
                                          GRPC_ARG_SERVICE_CONFIG) == nullptr) {
          return true;
        }
        builder->PrependFilter(&ServiceConfigChannelArgFilter, nullptr);
        return true;
      });
}

}  // namespace grpc_core
