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

#include <new>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/service_config/service_config_parser.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"

namespace grpc_core {

namespace {

class ServiceConfigChannelArgChannelData {
 public:
  explicit ServiceConfigChannelArgChannelData(
      const grpc_channel_element_args* args) {
    auto service_config_str =
        args->channel_args.GetOwnedString(GRPC_ARG_SERVICE_CONFIG);
    if (service_config_str.has_value()) {
      auto service_config = ServiceConfigImpl::Create(
          args->channel_args, service_config_str->c_str());
      if (!service_config.ok()) {
        gpr_log(GPR_ERROR, "%s", service_config.status().ToString().c_str());
      } else {
        service_config_ = std::move(*service_config);
      }
    }
  }

  RefCountedPtr<ServiceConfig> service_config() const {
    return service_config_;
  }

 private:
  RefCountedPtr<ServiceConfig> service_config_;
};

grpc_error_handle ServiceConfigChannelArgInitCallElem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  auto* chand =
      static_cast<ServiceConfigChannelArgChannelData*>(elem->channel_data);
  RefCountedPtr<ServiceConfig> service_config = chand->service_config();
  const ServiceConfigParser::ParsedConfigVector* method_configs = nullptr;
  if (service_config != nullptr) {
    method_configs = service_config->GetMethodParsedConfigVector(args->path);
  }
  auto* service_config_call_data =
      args->arena->New<ServiceConfigCallData>(args->arena, args->context);
  service_config_call_data->SetServiceConfig(std::move(service_config),
                                             method_configs);
  return absl::OkStatus();
}

void ServiceConfigChannelArgDestroyCallElem(
    grpc_call_element* /*elem*/, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*then_schedule_closure*/) {}

grpc_error_handle ServiceConfigChannelArgInitChannelElem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  ServiceConfigChannelArgChannelData* chand =
      static_cast<ServiceConfigChannelArgChannelData*>(elem->channel_data);
  new (chand) ServiceConfigChannelArgChannelData(args);
  return absl::OkStatus();
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
    0,
    ServiceConfigChannelArgInitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    ServiceConfigChannelArgDestroyCallElem,
    sizeof(ServiceConfigChannelArgChannelData),
    ServiceConfigChannelArgInitChannelElem,
    grpc_channel_stack_no_post_init,
    ServiceConfigChannelArgDestroyChannelElem,
    grpc_channel_next_get_info,
    "service_config_channel_arg"};

}  // namespace

void RegisterServiceConfigChannelArgFilter(
    CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        auto channel_args = builder->channel_args();
        if (channel_args.WantMinimalStack() ||
            !channel_args.GetString(GRPC_ARG_SERVICE_CONFIG).has_value()) {
          return true;
        }
        builder->PrependFilter(&ServiceConfigChannelArgFilter);
        return true;
      });
}

}  // namespace grpc_core
