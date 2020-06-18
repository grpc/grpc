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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/service_config_call_data.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_init.h"

namespace grpc_core {

namespace {

struct ServiceConfigChannelArgChannelData {
  grpc_core::RefCountedPtr<grpc_core::ServiceConfig> svc_cfg;
};

grpc_error* ServiceConfigChannelArgInitCallElem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  ServiceConfigChannelArgChannelData* chand =
      static_cast<ServiceConfigChannelArgChannelData*>(elem->channel_data);
  grpc_core::ServiceConfigCallData* svc_cfg_call_data = nullptr;
  if (args->context != nullptr) {
    svc_cfg_call_data = static_cast<grpc_core::ServiceConfigCallData*>(
        args->context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
    if (svc_cfg_call_data == nullptr && chand->svc_cfg != nullptr) {
      args->arena->New<ServiceConfigCallData>(
          chand->svc_cfg,
          chand->svc_cfg->GetMethodParsedConfigVector(args->path),
          args->context);
    }
  }
  return GRPC_ERROR_NONE;
}

void ServiceConfigChannelArgDestroyCallElem(
    grpc_call_element* /* elem */, const grpc_call_final_info* /* final_info */,
    grpc_closure* /* then_schedule_closure */) {}

grpc_error* ServiceConfigChannelArgInitChannelElem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  ServiceConfigChannelArgChannelData* chand =
      static_cast<ServiceConfigChannelArgChannelData*>(elem->channel_data);
  new (chand) ServiceConfigChannelArgChannelData();
  const char* service_config_str = grpc_channel_arg_get_string(
      grpc_channel_args_find(args->channel_args, GRPC_ARG_SERVICE_CONFIG));
  if (service_config_str != nullptr) {
    grpc_error* service_config_error = GRPC_ERROR_NONE;
    auto svc_cfg = grpc_core::ServiceConfig::Create(service_config_str,
                                                    &service_config_error);
    if (service_config_error == GRPC_ERROR_NONE) {
      chand->svc_cfg = std::move(svc_cfg);
    } else {
      gpr_log(GPR_ERROR, "%s", grpc_error_string(service_config_error));
    }
    GRPC_ERROR_UNREF(service_config_error);
  }
  return GRPC_ERROR_NONE;
}

void ServiceConfigChannelArgDestroyChannelElem(grpc_channel_element* elem) {
  ServiceConfigChannelArgChannelData* chand =
      static_cast<ServiceConfigChannelArgChannelData*>(elem->channel_data);
  chand->~ServiceConfigChannelArgChannelData();
}

const grpc_channel_filter ServiceConfigChannelArgFilter = {
    grpc_call_next_op,
    grpc_channel_next_op,
    0,
    ServiceConfigChannelArgInitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    ServiceConfigChannelArgDestroyCallElem,
    sizeof(ServiceConfigChannelArgChannelData),
    ServiceConfigChannelArgInitChannelElem,
    ServiceConfigChannelArgDestroyChannelElem,
    grpc_channel_next_get_info,
    "service_config_channel_arg"};

bool maybe_add_service_config_channel_arg_filter(
    grpc_channel_stack_builder* builder, void* /* arg */) {
  const grpc_channel_args* channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  if (grpc_channel_args_want_minimal_stack(channel_args) ||
      grpc_channel_arg_get_string(grpc_channel_args_find(
          channel_args, GRPC_ARG_SERVICE_CONFIG)) == nullptr) {
    return true;
  }
  return grpc_channel_stack_builder_prepend_filter(
      builder, &ServiceConfigChannelArgFilter, nullptr, nullptr);
}

}  // namespace

void grpc_service_config_channel_arg_filter_init(void) {
  grpc_channel_init_register_stage(
      GRPC_CLIENT_SUBCHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_service_config_channel_arg_filter, nullptr);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_service_config_channel_arg_filter, nullptr);
}

void grpc_service_config_channel_arg_filter_shutdown(void) {}

}  // namespace grpc_core
