//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CALL_PEER_ADDRESS_H
#define GRPC_SRC_CORE_CALL_PEER_ADDRESS_H

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

struct PeerAddress {
  Slice peer_address;
};

// Allow PeerAddress to be used as an arena context element.
template <>
struct ArenaContextType<PeerAddress> {
  static void Destroy(PeerAddress* peer_address) {
    peer_address->~PeerAddress();
  }
};

inline void SetPeerAddressContext(const Slice& peer_address_slice) {
  Arena* arena = GetContext<Arena>();
  auto* peer_address = arena->New<PeerAddress>();
  peer_address->peer_address = peer_address_slice.Ref();
  arena->SetContext<PeerAddress>(peer_address);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CALL_PEER_ADDRESS_H
