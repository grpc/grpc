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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_BINDER_CONNECTOR_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_BINDER_CONNECTOR_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include <grpc/impl/grpc_types.h>
#include <grpcpp/channel.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/client_channel/client_channel_factory.h"
#include "src/core/client_channel/client_channel_filter.h"

namespace grpc_core {

class BinderClientChannelFactory : public ClientChannelFactory {
 public:
  RefCountedPtr<Subchannel> CreateSubchannel(
      const grpc_resolved_address& address, const ChannelArgs& args) override;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_BINDER_CONNECTOR_H
