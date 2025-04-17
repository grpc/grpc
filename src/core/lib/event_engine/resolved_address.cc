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

#include "src/core/lib/iomgr/resolved_address.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include "absl/log/check.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"

// IWYU pragma: no_include <sys/socket.h>

namespace grpc_event_engine::experimental {

EventEngine::ResolvedAddress::ResolvedAddress(const sockaddr* address,
                                              socklen_t size)
    : size_(size) {
  DCHECK_GE(size, 0u);
  CHECK(static_cast<size_t>(size) <= sizeof(address_));
  memcpy(&address_, address, size);
}

const struct sockaddr* EventEngine::ResolvedAddress::address() const {
  return reinterpret_cast<const struct sockaddr*>(address_);
}

socklen_t EventEngine::ResolvedAddress::size() const { return size_; }

EventEngine::ResolvedAddress CreateResolvedAddress(
    const grpc_resolved_address& addr) {
  return EventEngine::ResolvedAddress(
      reinterpret_cast<const sockaddr*>(addr.addr), addr.len);
}

grpc_resolved_address CreateGRPCResolvedAddress(
    const EventEngine::ResolvedAddress& ra) {
  static_assert(
      GRPC_MAX_SOCKADDR_SIZE == EventEngine::ResolvedAddress::MAX_SIZE_BYTES,
      "size should match");
  grpc_resolved_address grpc_addr;
  memset(&grpc_addr, 0, sizeof(grpc_resolved_address));
  memcpy(grpc_addr.addr, ra.address(), ra.size());
  grpc_addr.len = ra.size();
  return grpc_addr;
}

}  // namespace grpc_event_engine::experimental
