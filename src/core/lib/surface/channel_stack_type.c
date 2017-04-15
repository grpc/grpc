/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/surface/channel_stack_type.h"
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

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

const char *grpc_channel_stack_type_string(grpc_channel_stack_type type) {
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
