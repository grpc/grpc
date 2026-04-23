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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_STACK_TYPE_H
#define GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_STACK_TYPE_H

// Normally (when dynamic filters are not used), the server creates a
// single filter stack of type GRPC_SERVER_CHANNEL that contains all
// necessary filters.
//
// However, when using dynamic filters are used (i.e., when there is a
// ServerConfigSelectorProvider object in channel args), the filters are
// split into 3 stacks:
//
// - The top filter stack, of type GRPC_SERVER_TOP_CHANNEL.
//
// - The dynamic filter stack, which is dynamically configured.
//
// - The bottom filter stack, of type GRPC_SERVER_CHANNEL, which will
//   always have the GRPC_ARG_BELOW_DYNAMIC_FILTERS channel arg set.
//
// In this case, the server creates a filter stack of type
// GRPC_SERVER_TOP_CHANNEL.  The last filter in this stack is
// ServerConfigSelectorFilter, which is responsible for creating the
// dynamic filter stacks and the bottom filter stack.  It will then use
// the ServerConfigSelector to determine which dynamic filter stack to use
// for each RPC.
//
// Note that we use the GRPC_SERVER_CHANNEL filter stack type for both the
// single stack when dynamic filters are not used and for the bottom stack
// when dynamic filters are used.  Therefore, filters that need to run above
// dynamic filters should be registered twice:
// 1. For channel stack type GRPC_SERVER_CHANNEL, with a restriction
//    that this channel arg must NOT be set.
// 2. For channel stack type GRPC_SERVER_TOP_CHANNEL.
// This ensures that they are in the right place in both modes.
#define GRPC_ARG_BELOW_DYNAMIC_FILTERS "grpc.internal.below_dynamic_filters"

typedef enum {
  // normal top-half client channel with load-balancing, connection management
  GRPC_CLIENT_CHANNEL,
  // bottom-half of a client channel: everything that happens post-load
  // balancing (bound to a specific transport)
  GRPC_CLIENT_SUBCHANNEL,
  // dynamic part of a client channel
  GRPC_CLIENT_DYNAMIC,
  // a permanently broken client channel
  GRPC_CLIENT_LAME_CHANNEL,
  // a directly connected client channel (without load-balancing, directly talks
  // to a transport)
  GRPC_CLIENT_DIRECT_CHANNEL,
  // server side channel
  GRPC_SERVER_CHANNEL,
  // server top channel (above dynamic filters)
  GRPC_SERVER_TOP_CHANNEL,
  // must be last
  GRPC_NUM_CHANNEL_STACK_TYPES
} grpc_channel_stack_type;

bool grpc_channel_stack_type_is_client(grpc_channel_stack_type type);

const char* grpc_channel_stack_type_string(grpc_channel_stack_type type);

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_STACK_TYPE_H
