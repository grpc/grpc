/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACE_REGISTRY_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACE_REGISTRY_H

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/channel/channel_trace.h"

#include <stdint.h>

// TODO(ncteisen): convert this file to C++

void grpc_channel_trace_registry_init();
void grpc_channel_trace_registry_shutdown();

// globally registers a ChannelTrace. Returns its unique uuid
intptr_t grpc_channel_trace_registry_register_channel_trace(
    grpc_core::ChannelTrace* channel_trace);
// globally unregisters the ChannelTrace that is associated to uuid.
void grpc_channel_trace_registry_unregister_channel_trace(intptr_t uuid);
// if object with uuid has previously been registered, returns the ChannelTrace
// associated with that uuid. Else returns nullptr.
grpc_core::ChannelTrace* grpc_channel_trace_registry_get_channel_trace(
    intptr_t uuid);

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACE_REGISTRY_H */
