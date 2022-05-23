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

#include <grpcpp/ext/admin_services.h>
#include <grpcpp/server_builder.h>

// TODO(lidiz) build a real registration system that can pull in services
// automatically with minimum amount of code.
#include "src/cpp/server/channelz/channelz_service.h"
#if !defined(GRPC_NO_XDS) && !defined(DISABLED_XDS_PROTO_IN_CC)
#include "src/cpp/server/csds/csds.h"
#endif  // GRPC_NO_XDS or DISABLED_XDS_PROTO_IN_CC
namespace grpc {

namespace {

auto* g_channelz_service = new ChannelzService();
#if !defined(GRPC_NO_XDS) && !defined(DISABLED_XDS_PROTO_IN_CC)
auto* g_csds = new xds::experimental::ClientStatusDiscoveryService();
#endif  // GRPC_NO_XDS or DISABLED_XDS_PROTO_IN_CC

}  // namespace

void AddAdminServices(ServerBuilder* builder) {
  builder->RegisterService(g_channelz_service);
#if !defined(GRPC_NO_XDS) && !defined(DISABLED_XDS_PROTO_IN_CC)
  builder->RegisterService(g_csds);
#endif  // GRPC_NO_XDS or DISABLED_XDS_PROTO_IN_CC
}

}  // namespace grpc
