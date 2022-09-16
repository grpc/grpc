/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/channel_stack_type.h"

bool grpc_channel_stack_type_is_client(grpc_channel_stack_type type) {
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
      break;
  }
  GPR_UNREACHABLE_CODE(return true;);
}

const char* grpc_channel_stack_type_string(grpc_channel_stack_type type) {
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
      break;
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}
