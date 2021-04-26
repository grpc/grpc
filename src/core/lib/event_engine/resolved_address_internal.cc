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
#ifdef GRPC_EVENT_ENGINE_TEST

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/iomgr/event_engine/resolved_address_internal.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

namespace grpc_event_engine {
namespace experimental {

std::string ResolvedAddressToURI(const EventEngine::ResolvedAddress& addr) {
  auto gra = CreateGRPCResolvedAddress(addr);
  return grpc_sockaddr_to_uri(&gra);
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif
