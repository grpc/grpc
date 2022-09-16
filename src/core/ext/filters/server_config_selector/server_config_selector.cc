//
// Copyright 2021 gRPC authors.
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

#include "src/core/ext/filters/server_config_selector/server_config_selector.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"

namespace grpc_core {
namespace {

void* ServerConfigSelectorProviderArgCopy(void* p) {
  ServerConfigSelectorProvider* arg =
      static_cast<ServerConfigSelectorProvider*>(p);
  return arg->Ref().release();
}

void ServerConfigSelectorProviderArgDestroy(void* p) {
  ServerConfigSelectorProvider* arg =
      static_cast<ServerConfigSelectorProvider*>(p);
  arg->Unref();
}

int ServerConfigSelectorProviderArgCmp(void* p, void* q) {
  return QsortCompare(p, q);
}

const grpc_arg_pointer_vtable kChannelArgVtable = {
    ServerConfigSelectorProviderArgCopy, ServerConfigSelectorProviderArgDestroy,
    ServerConfigSelectorProviderArgCmp};

const char* kServerConfigSelectorProviderChannelArgName =
    "grpc.internal.server_config_selector_provider";

}  // namespace

grpc_arg ServerConfigSelectorProvider::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(kServerConfigSelectorProviderChannelArgName),
      const_cast<ServerConfigSelectorProvider*>(this), &kChannelArgVtable);
}

absl::string_view ServerConfigSelectorProvider::ChannelArgName() {
  return kServerConfigSelectorProviderChannelArgName;
}

}  // namespace grpc_core
