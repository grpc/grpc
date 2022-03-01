// Copyright 2021 The gRPC Authors
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

#include "src/core/lib/iomgr/event_engine/resolved_address_internal.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/resolve_address.h"

namespace grpc_event_engine {
namespace experimental {

EventEngine::ResolvedAddress CreateResolvedAddress(
    const grpc_resolved_address& addr) {
  return EventEngine::ResolvedAddress(
      reinterpret_cast<const sockaddr*>(addr.addr), addr.len);
}

grpc_resolved_address CreateGRPCResolvedAddress(
    const EventEngine::ResolvedAddress& ra) {
  grpc_resolved_address grpc_addr;
  memcpy(grpc_addr.addr, ra.address(), ra.size());
  grpc_addr.len = ra.size();
  return grpc_addr;
}

// TODO(ctiller): Move this to somewhere more permanent as we're deleting iomgr.
std::string ResolvedAddressToURI(const EventEngine::ResolvedAddress& addr) {
  auto gra = CreateGRPCResolvedAddress(addr);
  return grpc_sockaddr_to_uri(&gra);
}

}  // namespace experimental
}  // namespace grpc_event_engine
