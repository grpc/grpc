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

#include <functional>
#include <memory>
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
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/service_config/service_config_parser.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

namespace {

class ServiceConfigChannelArgFilter : public ChannelFilter {
 public:
  static absl::StatusOr<ServiceConfigChannelArgFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args) {
    return ServiceConfigChannelArgFilter(args);
  }

  explicit ServiceConfigChannelArgFilter(const ChannelArgs& args) {
    auto service_config_str = args.GetOwnedString(GRPC_ARG_SERVICE_CONFIG);
    if (service_config_str.has_value()) {
      auto service_config =
          ServiceConfigImpl::Create(args, *service_config_str);
      if (!service_config.ok()) {
        gpr_log(GPR_ERROR, "%s", service_config.status().ToString().c_str());
      } else {
        service_config_ = std::move(*service_config);
      }
    }
  }

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  RefCountedPtr<ServiceConfig> service_config_;
};

ArenaPromise<ServerMetadataHandle>
ServiceConfigChannelArgFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  const ServiceConfigParser::ParsedConfigVector* method_configs = nullptr;
  if (service_config_ != nullptr) {
    method_configs = service_config_->GetMethodParsedConfigVector(
        call_args.client_initial_metadata->get_pointer(HttpPathMetadata())
            ->c_slice());
  }
  auto* arena = GetContext<Arena>();
  auto* service_config_call_data = arena->New<ServiceConfigCallData>(
      arena, GetContext<grpc_call_context_element>());
  service_config_call_data->SetServiceConfig(service_config_, method_configs);
  return next_promise_factory(std::move(call_args));
}

const grpc_channel_filter kServiceConfigChannelArgFilter =
    MakePromiseBasedFilter<ServiceConfigChannelArgFilter,
                           FilterEndpoint::kClient>(
        "service_config_channel_arg");

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
        builder->PrependFilter(&kServiceConfigChannelArgFilter);
        return true;
      });
}

}  // namespace grpc_core
