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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_CHANNEL_FACTORY_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_CHANNEL_FACTORY_H

#include <grpc/channel_factory.h>

#include <memory>
#include <utility>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_transport.h"

namespace grpc_core {

// The concrete experimental::ChannelFactory::ChannelHandle returned by the
// factories. Callers can DownCast to this type to get the XdsTransport.
class ChannelHandleImpl final
    : public experimental::ChannelFactory::ChannelHandle {
 public:
  explicit ChannelHandleImpl(RefCountedPtr<XdsTransport> transport)
      : transport_(std::move(transport)) {}

  const RefCountedPtr<XdsTransport>& transport() const { return transport_; }

 private:
  RefCountedPtr<XdsTransport> transport_;
};

// Extracts ChannelFactory from ChannelArgs. Returns nullptr if not set.
std::shared_ptr<experimental::ChannelFactory> GetChannelFactoryFromChannelArgs(
    const ChannelArgs& args);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_CHANNEL_FACTORY_H
