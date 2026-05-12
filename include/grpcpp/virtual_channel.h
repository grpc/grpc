//
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
//

#ifndef GRPCPP_VIRTUAL_CHANNEL_H
#define GRPCPP_VIRTUAL_CHANNEL_H

#include <grpcpp/channel.h>
#include <grpcpp/impl/call.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/config.h>

#include <memory>

namespace grpc {
namespace experimental {
/// Create a new \em custom virtual \a Channel using \a call.
///
/// \warning For internal gRPC use ONLY.
///
/// \param call The call object to create the virtual channel from.
std::shared_ptr<Channel> CreateVirtualChannel(grpc::internal::Call call);

/// Create a new \em custom virtual \a Channel using \a call.
///
/// \warning For internal gRPC use ONLY. Override default channel
/// arguments only if necessary.
///
/// \param call The call object to create the virtual channel from.
/// \param args Options for channel creation.
std::shared_ptr<Channel> CreateVirtualChannel(grpc::internal::Call call,
                                              const ChannelArguments& args);
}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_VIRTUAL_CHANNEL_H
