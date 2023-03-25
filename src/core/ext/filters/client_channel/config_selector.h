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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_SELECTOR_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_SELECTOR_H

#include <grpc/support/port_platform.h>

#include <string.h>

#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/impl/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/service_config/service_config_parser.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"

// Channel arg key for ConfigSelector.
#define GRPC_ARG_CONFIG_SELECTOR "grpc.internal.config_selector"

namespace grpc_core {

// Internal API used to allow resolver implementations to override
// MethodConfig and provide input to LB policies on a per-call basis.
class ConfigSelector : public RefCounted<ConfigSelector> {
 public:
  // An interface to be used by the channel when dispatching calls.
  class CallDispatchController {
   public:
    virtual ~CallDispatchController() = default;

    // Called by the channel to decide if it should retry the call upon a
    // failure.
    virtual bool ShouldRetry() = 0;

    // Called by the channel when no more LB picks will be performed for
    // the call.
    virtual void Commit() = 0;
  };

  struct GetCallConfigArgs {
    grpc_metadata_batch* initial_metadata;
    Arena* arena;
  };

  struct CallConfig {
    // The per-method parsed configs that will be passed to
    // ServiceConfigCallData.
    const ServiceConfigParser::ParsedConfigVector* method_configs = nullptr;
    // A ref to the service config that contains method_configs, held by
    // the call to ensure that method_configs lives long enough.
    RefCountedPtr<ServiceConfig> service_config;
    // Call attributes that will be accessible to LB policy implementations.
    ServiceConfigCallData::CallAttributes call_attributes;
    // Call dispatch controller.
    CallDispatchController* call_dispatch_controller = nullptr;
  };

  ~ConfigSelector() override = default;

  virtual const char* name() const = 0;

  static bool Equals(const ConfigSelector* cs1, const ConfigSelector* cs2) {
    if (cs1 == nullptr) return cs2 == nullptr;
    if (cs2 == nullptr) return false;
    if (strcmp(cs1->name(), cs2->name()) != 0) return false;
    return cs1->Equals(cs2);
  }

  // The channel will call this when the resolver returns a new ConfigSelector
  // to determine what set of dynamic filters will be configured.
  virtual std::vector<const grpc_channel_filter*> GetFilters() { return {}; }

  // Returns the call config to use for the call, or a status to fail
  // the call with.
  virtual absl::StatusOr<CallConfig> GetCallConfig(GetCallConfigArgs args) = 0;

  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<ConfigSelector> GetFromChannelArgs(
      const grpc_channel_args& args);
  static absl::string_view ChannelArgName() { return GRPC_ARG_CONFIG_SELECTOR; }
  static int ChannelArgsCompare(const ConfigSelector* a,
                                const ConfigSelector* b) {
    return QsortCompare(a, b);
  }

 private:
  // Will be called only if the two objects have the same name, so
  // subclasses can be free to safely down-cast the argument.
  virtual bool Equals(const ConfigSelector* other) const = 0;
};

// Default ConfigSelector that gets the MethodConfig from the service config.
class DefaultConfigSelector : public ConfigSelector {
 public:
  explicit DefaultConfigSelector(RefCountedPtr<ServiceConfig> service_config)
      : service_config_(std::move(service_config)) {
    // The client channel code ensures that this will never be null.
    // If neither the resolver nor the client application provide a
    // config, a default empty config will be used.
    GPR_DEBUG_ASSERT(service_config_ != nullptr);
  }

  const char* name() const override { return "default"; }

  absl::StatusOr<CallConfig> GetCallConfig(GetCallConfigArgs args) override {
    CallConfig call_config;
    Slice* path = args.initial_metadata->get_pointer(HttpPathMetadata());
    GPR_ASSERT(path != nullptr);
    call_config.method_configs =
        service_config_->GetMethodParsedConfigVector(path->c_slice());
    call_config.service_config = service_config_;
    return call_config;
  }

  // Only comparing the ConfigSelector itself, not the underlying
  // service config, so we always return true.
  bool Equals(const ConfigSelector* /*other*/) const override { return true; }

 private:
  RefCountedPtr<ServiceConfig> service_config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_SELECTOR_H
