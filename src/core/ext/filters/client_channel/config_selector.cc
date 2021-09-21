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

#include "src/core/ext/filters/client_channel/config_selector.h"

#include "src/core/lib/channel/channel_args.h"

namespace grpc_core {

namespace {

void* ConfigSelectorArgCopy(void* p) {
  ConfigSelector* config_selector = static_cast<ConfigSelector*>(p);
  config_selector->Ref().release();
  return p;
}

void ConfigSelectorArgDestroy(void* p) {
  ConfigSelector* config_selector = static_cast<ConfigSelector*>(p);
  config_selector->Unref();
}

int ConfigSelectorArgCmp(void* p, void* q) { return QsortCompare(p, q); }

const grpc_arg_pointer_vtable kChannelArgVtable = {
    ConfigSelectorArgCopy, ConfigSelectorArgDestroy, ConfigSelectorArgCmp};

}  // namespace

grpc_arg ConfigSelector::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CONFIG_SELECTOR),
      const_cast<ConfigSelector*>(this), &kChannelArgVtable);
}

RefCountedPtr<ConfigSelector> ConfigSelector::GetFromChannelArgs(
    const grpc_channel_args& args) {
  ConfigSelector* config_selector =
      grpc_channel_args_find_pointer<ConfigSelector>(&args,
                                                     GRPC_ARG_CONFIG_SELECTOR);
  return config_selector != nullptr ? config_selector->Ref() : nullptr;
}

}  // namespace grpc_core
