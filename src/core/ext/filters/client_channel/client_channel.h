/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_H

#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/lib/channel/channel_stack.h"

extern grpc_core::TraceFlag grpc_client_channel_trace;

// Channel arg key for server URI string.
#define GRPC_ARG_SERVER_URI "grpc.server_uri"

#ifdef __cplusplus
extern "C" {
#endif

/* A client channel is a channel that begins disconnected, and can connect
   to some endpoint on demand. If that endpoint disconnects, it will be
   connected to again later.

   Calls on a disconnected client channel are queued until a connection is
   established. */

extern const grpc_channel_filter grpc_client_channel_filter;

grpc_connectivity_state grpc_client_channel_check_connectivity_state(
    grpc_exec_ctx* exec_ctx, grpc_channel_element* elem, int try_to_connect);

int grpc_client_channel_num_external_connectivity_watchers(
    grpc_channel_element* elem);

void grpc_client_channel_watch_connectivity_state(
    grpc_exec_ctx* exec_ctx, grpc_channel_element* elem,
    grpc_polling_entity pollent, grpc_connectivity_state* state,
    grpc_closure* on_complete, grpc_closure* watcher_timer_init);

/* Debug helper: pull the subchannel call from a call stack element */
grpc_subchannel_call* grpc_client_channel_get_subchannel_call(
    grpc_call_element* elem);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_H */
