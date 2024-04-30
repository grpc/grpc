//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/handshaker/handshaker_registry.h"

#include <stddef.h>

#include <algorithm>
#include <utility>

#include <grpc/support/port_platform.h>

namespace grpc_core {

void HandshakerRegistry::Builder::RegisterHandshakerFactory(
    HandshakerType handshaker_type,
    std::unique_ptr<HandshakerFactory> factory) {
  auto& vec = factories_[handshaker_type];
  auto where = vec.empty() ? vec.begin() : vec.end();
  for (auto iter = vec.begin(); iter != vec.end(); ++iter) {
    if (factory->Priority() < iter->get()->Priority()) {
      where = iter;
      break;
    }
  }
  vec.insert(where, std::move(factory));
}

HandshakerRegistry HandshakerRegistry::Builder::Build() {
  HandshakerRegistry out;
  for (size_t i = 0; i < NUM_HANDSHAKER_TYPES; i++) {
    out.factories_[i] = std::move(factories_[i]);
  }
  return out;
}

void HandshakerRegistry::AddHandshakers(HandshakerType handshaker_type,
                                        const ChannelArgs& args,
                                        grpc_pollset_set* interested_parties,
                                        HandshakeManager* handshake_mgr) const {
  for (const auto& factory : factories_[handshaker_type]) {
    factory->AddHandshakers(args, interested_parties, handshake_mgr);
  }
}

}  // namespace grpc_core
