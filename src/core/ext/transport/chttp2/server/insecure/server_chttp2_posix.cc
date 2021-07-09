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

#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"

// TODO(hork): add channel_args to this API to allow endpoints and transports
// created in this function to participate in the resource quota feature.
void grpc_server_add_insecure_channel_from_fd(grpc_server* server,
                                              void* reserved, int fd) {
  GPR_ASSERT(reserved == nullptr);

  grpc_core::ExecCtx exec_ctx;
  grpc_core::Server* core_server = server->core_server.get();

  const grpc_channel_args* server_args = core_server->channel_args();
  std::string name = absl::StrCat("fd:", fd);
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create(name.c_str());
  grpc_resource_user* resource_user =
      grpc_resource_user_create(resource_quota, name.c_str());
  grpc_resource_quota_unref_internal(resource_quota);
  grpc_endpoint* server_endpoint =
      grpc_tcp_create(grpc_fd_create(fd, name.c_str(), true), server_args,
                      name.c_str(), resource_user);
  grpc_transport* transport = grpc_create_chttp2_transport(
      server_args, server_endpoint, false /* is_client */, resource_user);
  grpc_resource_user_unref(resource_user);
  grpc_error_handle error =
      core_server->SetupTransport(transport, nullptr, server_args, nullptr);
  if (error == GRPC_ERROR_NONE) {
    for (grpc_pollset* pollset : core_server->pollsets()) {
      grpc_endpoint_add_to_pollset(server_endpoint, pollset);
    }
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
  } else {
    gpr_log(GPR_ERROR, "Failed to create channel: %s",
            grpc_error_std_string(error).c_str());
    GRPC_ERROR_UNREF(error);
    grpc_transport_destroy(transport);
  }
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

void grpc_server_add_insecure_channel_from_fd(grpc_server* /* server */,
                                              void* /* reserved */,
                                              int /* fd */) {
  GPR_ASSERT(0);
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD
