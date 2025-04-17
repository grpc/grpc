//
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
//

#include "src/core/client_channel/client_channel_factory.h"

#include <grpc/support/port_platform.h>

// Channel arg key for client channel factory.
#define GRPC_ARG_CLIENT_CHANNEL_FACTORY "grpc.client_channel_factory"

namespace grpc_core {

absl::string_view ClientChannelFactory::ChannelArgName() {
  return GRPC_ARG_CLIENT_CHANNEL_FACTORY;
}

}  // namespace grpc_core
