//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_FACTORY_H
#define GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_FACTORY_H

#include <grpc/grpc.h>

#include <memory>

#include "src/core/lib/surface/channel.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

// Returns a shared_ptr that owns the ref. The deleter unrefs the channel.
std::shared_ptr<Channel> MakeSharedChannel(RefCountedPtr<Channel> channel);

// Vtable for a GRPC_ARG_CHANNEL_FACTORY arg. The arg value is a
// std::shared_ptr<experimental::ChannelFactory>*.
const grpc_arg_pointer_vtable* ChannelFactoryArgVtable();

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_FACTORY_H
