//
// Copyright 2026 gRPC authors.
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

#include "src/core/lib/surface/channel_factory.h"

#include <grpc/channel_factory.h>
#include <grpc/grpc.h>

#include <memory>
#include <string>
#include <utility>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

std::shared_ptr<Channel> MakeSharedChannel(RefCountedPtr<Channel> channel) {
  return std::shared_ptr<Channel>(channel.release(), [](Channel* c) {
    ExecCtx exec_ctx;
    c->Unref();
  });
}

const grpc_arg_pointer_vtable* ChannelFactoryArgVtable() {
  return ChannelArgTypeTraits<
      std::shared_ptr<experimental::ChannelFactory>>::VTable();
}

namespace experimental {

std::shared_ptr<Channel> ChannelFactory::CreateCoreChannel(
    absl::string_view target, grpc_channel_credentials* creds,
    const grpc_channel_args* args) {
  ExecCtx exec_ctx;
  const std::string target_str(target);
  ChannelArgs channel_args = CoreConfiguration::Get()
                                 .channel_args_preconditioning()
                                 .PreconditionChannelArgs(args);
  absl::StatusOr<grpc_channel*> channel =
      CreateClientEndpointChannel(target_str.c_str(), creds, channel_args);
  if (!channel.ok()) {
    return MakeSharedChannel(MakeLameChannel(
        target,
        absl::Status(channel.status().code(),
                     absl::StrCat("Failed to create channel to '", target,
                                  "':", channel.status().message()))));
  }
  // Take ownership of the ref held by the new grpc_channel*.
  return MakeSharedChannel(RefCountedPtr<Channel>(Channel::FromC(*channel)));
}

std::shared_ptr<Channel> ChannelFactory::CreateLameChannel(
    absl::string_view target, absl::Status status) {
  ExecCtx exec_ctx;
  return MakeSharedChannel(MakeLameChannel(target, std::move(status)));
}

}  // namespace experimental
}  // namespace grpc_core
