//
// Copyright 2019 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpcpp/support/client_callback.h>
#include <grpcpp/support/client_interceptor.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <vector>

#include "src/core/client_channel/virtual_channel.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/call.h"
#include "src/cpp/client/create_channel_internal.h"
namespace grpc {
namespace internal {

std::shared_ptr<grpc::Channel> CreateVirtualChannel(grpc_call* call) {
  grpc_core::ExecCtx exec_ctx;
  // TODO(snohria): Pass in the correct channel args.
  auto args = grpc_core::ChannelArgs();
  args = args.SetObject(grpc_core::ResourceQuota::Default())
             .Set(GRPC_ARG_DEFAULT_AUTHORITY, "virtual_target")
             .Set(GRPC_ARG_MINIMAL_STACK, 1);

  auto core_channel = grpc_core::VirtualChannel::Create(call, args);
  GRPC_CHECK(core_channel.ok());

  return grpc::CreateChannelInternal(
      "", core_channel->release()->c_ptr(),
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>());
}

bool ClientReactor::InternalTrailersOnly(const grpc_call* call) const {
  return grpc_call_is_trailers_only(call);
}

}  // namespace internal
}  // namespace grpc
