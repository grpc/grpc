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

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/support/log.h>

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

#include <fcntl.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/tcp_client_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"

grpc_channel* grpc_insecure_channel_create_from_fd(
    const char* target, int fd, const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_insecure_channel_create(target=%p, fd=%d, args=%p)", 3,
                 (target, fd, args));

  grpc_arg default_authority_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
      const_cast<char*>("test.authority"));
  args = grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);
  const grpc_channel_args* final_args = grpc_core::CoreConfiguration::Get()
                                            .channel_args_preconditioning()
                                            .PreconditionChannelArgs(args);
  grpc_channel_args_destroy(args);

  int flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
  grpc_endpoint* client = grpc_tcp_client_create_from_fd(
      grpc_fd_create(fd, "client", true), final_args, "fd-client");
  grpc_transport* transport =
      grpc_create_chttp2_transport(final_args, client, true);
  GPR_ASSERT(transport);
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_channel* channel = grpc_channel_create(
      target, final_args, GRPC_CLIENT_DIRECT_CHANNEL, transport, &error);
  grpc_channel_args_destroy(final_args);
  if (channel != nullptr) {
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    grpc_core::ExecCtx::Get()->Flush();
  } else {
    intptr_t integer;
    grpc_status_code status = GRPC_STATUS_INTERNAL;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
      status = static_cast<grpc_status_code>(integer);
    }
    GRPC_ERROR_UNREF(error);
    grpc_transport_destroy(transport);
    channel = grpc_lame_client_channel_create(
        target, status, "Failed to create client channel");
  }

  return channel;
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

grpc_channel* grpc_insecure_channel_create_from_fd(
    const char* /* target */, int /* fd */,
    const grpc_channel_args* /* args */) {
  GPR_ASSERT(0);
  return nullptr;
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD
