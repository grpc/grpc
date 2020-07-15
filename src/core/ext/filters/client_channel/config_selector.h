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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_SELECTOR_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_SELECTOR_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <map>

#include "absl/strings/string_view.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/impl/codegen/slice.h>

#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/service_config_parser.h"
#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// Internal API used to allow resolver implementations to override
// MethodConfig and provide input to LB policies on a per-call basis.
class ConfigSelector : public RefCounted<ConfigSelector> {
 public:
  struct GetCallConfigArgs {
    grpc_slice* path;
    grpc_metadata_batch* initial_metadata;
    Arena* arena;
  };

  struct CallConfig {
    // Can be set to indicate the call should be failed.
    grpc_error* error = GRPC_ERROR_NONE;
    // The per-method parsed configs that will be passed to
    // ServiceConfigCallData.
    const ServiceConfigParser::ParsedConfigVector* method_configs = nullptr;
    // Call attributes that will be accessible to LB policy implementations.
    std::map<const char*, absl::string_view> call_attributes;
    // A callback that, if set, will be invoked when the call is
    // committed (i.e., when we know that we will never again need to
    // ask the picker for a subchannel for this call).
    std::function<void()> on_call_committed;
  };

  virtual ~ConfigSelector() = default;

  virtual CallConfig GetCallConfig(GetCallConfigArgs args) = 0;

  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<ConfigSelector> GetFromChannelArgs(
      const grpc_channel_args& args);
};

// Default ConfigSelector that gets the MethodConfig from the service config.
class DefaultConfigSelector : public ConfigSelector {
 public:
  explicit DefaultConfigSelector(RefCountedPtr<ServiceConfig> service_config)
      : service_config_(std::move(service_config)) {}

  CallConfig GetCallConfig(GetCallConfigArgs args) override {
    CallConfig call_config;
    if (service_config_ != nullptr) {
      call_config.method_configs =
          service_config_->GetMethodParsedConfigVector(*args.path);
    }
    return call_config;
  }

 private:
  RefCountedPtr<ServiceConfig> service_config_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_SELECTOR_H */
