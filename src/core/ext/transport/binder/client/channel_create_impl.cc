// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/client/channel_create_impl.h"

#include <memory>
#include <utility>

#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"

namespace grpc {
namespace internal {

grpc_channel* CreateChannelFromBinderImpl(
    std::unique_ptr<grpc_binder::Binder> endpoint_binder,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy,
    const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_channel_create_from_binder(target=%p, args=%p)", 2,
                 ((void*)1234, args));

  grpc_transport* transport = grpc_create_binder_transport_client(
      std::move(endpoint_binder), security_policy);
  GPR_ASSERT(transport);

  // TODO(b/192207753): check binder alive and ping binder

  // TODO(b/192207758): Figure out if we are required to set authority here
  grpc_arg default_authority_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
      const_cast<char*>("test.authority"));
  grpc_channel_args* final_args =
      grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_channel* channel = grpc_channel_create(
      "binder_target_placeholder", final_args, GRPC_CLIENT_DIRECT_CHANNEL,
      transport, nullptr, 0, &error);
  // TODO(mingcl): Handle error properly
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_channel_args_destroy(final_args);
  return channel;
}

}  // namespace internal
}  // namespace grpc
