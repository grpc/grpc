// Copyright 2015-2016 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_COMMON_CHANNEL_ARGS_H
#define GRPC_SRC_CORE_LIB_SURFACE_COMMON_CHANNEL_ARGS_H

#include "src/core/lib/channel/channel_args.h"

namespace grpc_core {
extern ChannelArgs::StringKey kDefaultAuthorityKey;
}

#endif