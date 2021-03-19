//
//
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
//
//

#include <grpc/support/port_platform.h>

#include "absl/memory/memory.h"

#include <grpcpp/ext/admin_services.h>
#include <grpcpp/impl/server_builder_plugin.h>

#include <grpcpp/server_builder.h>

#include "src/cpp/server/channelz/channelz_service.h"
#ifndef GRPC_NO_XDS
#include "src/cpp/server/csds/csds.h"
#endif  // GRPC_NO_XDS
namespace grpc {

namespace experimental {

namespace {

auto g_channelz_service = absl::make_unique<ChannelzService>();
#ifndef GRPC_NO_XDS
auto g_csds =
    absl::make_unique<xds::experimental::ClientStatusDiscoveryService>();
#endif  // GRPC_NO_XDS

}  // namespace

void AddAdminServices(ServerBuilder* builder) {
  builder->RegisterService(g_channelz_service.get());
#ifndef GRPC_NO_XDS
  builder->RegisterService(g_csds.get());
#endif  // GRPC_NO_XDS
}

}  // namespace experimental
}  // namespace grpc
