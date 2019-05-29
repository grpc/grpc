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

#ifndef GRPC_CORE_LIB_SURFACE_CHANNEL_STACK_TYPE_H
#define GRPC_CORE_LIB_SURFACE_CHANNEL_STACK_TYPE_H

#include <grpc/support/port_platform.h>

#include <stdbool.h>

typedef enum {
  // normal top-half client channel with load-balancing, connection management
  GRPC_CLIENT_CHANNEL,
  // bottom-half of a client channel: everything that happens post-load
  // balancing (bound to a specific transport)
  GRPC_CLIENT_SUBCHANNEL,
  // a permanently broken client channel
  GRPC_CLIENT_LAME_CHANNEL,
  // a directly connected client channel (without load-balancing, directly talks
  // to a transport)
  GRPC_CLIENT_DIRECT_CHANNEL,
  // server side channel
  GRPC_SERVER_CHANNEL,
  // must be last
  GRPC_NUM_CHANNEL_STACK_TYPES
} grpc_channel_stack_type;

inline bool grpc_channel_stack_type_is_client(grpc_channel_stack_type type) {
  GPR_DEBUG_ASSERT(type >= GRPC_CLIENT_CHANNEL &&
                   type < GRPC_NUM_CHANNEL_STACK_TYPES);
  switch (type) {
    case GRPC_CLIENT_CHANNEL:
      return true;
    case GRPC_CLIENT_SUBCHANNEL:
      return true;
    case GRPC_CLIENT_LAME_CHANNEL:
      return true;
    case GRPC_CLIENT_DIRECT_CHANNEL:
      return true;
    case GRPC_SERVER_CHANNEL:
      return false;
    case GRPC_NUM_CHANNEL_STACK_TYPES:
      return true;
  }
}

inline const char* grpc_channel_stack_type_string(
    grpc_channel_stack_type type) {
  GPR_DEBUG_ASSERT(type >= GRPC_CLIENT_CHANNEL &&
                   type < GRPC_NUM_CHANNEL_STACK_TYPES);
  switch (type) {
    case GRPC_CLIENT_CHANNEL:
      return "CLIENT_CHANNEL";
    case GRPC_CLIENT_SUBCHANNEL:
      return "CLIENT_SUBCHANNEL";
    case GRPC_SERVER_CHANNEL:
      return "SERVER_CHANNEL";
    case GRPC_CLIENT_LAME_CHANNEL:
      return "CLIENT_LAME_CHANNEL";
    case GRPC_CLIENT_DIRECT_CHANNEL:
      return "CLIENT_DIRECT_CHANNEL";
    case GRPC_NUM_CHANNEL_STACK_TYPES:
      return "UNKNOWN";
  }
}

#endif /* GRPC_CORE_LIB_SURFACE_CHANNEL_STACK_TYPE_H */
