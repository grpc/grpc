/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/memory.h"

#include <string.h>
#include <algorithm>

#include <grpc/support/alloc.h>

//
// grpc_handshaker_factory_list
//

namespace grpc_core {

namespace {

class HandshakerFactoryList {
 public:
  void Register(bool at_start, std::unique_ptr<HandshakerFactory> factory);
  void AddHandshakers(const grpc_channel_args* args,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr);

 private:
  InlinedVector<std::unique_ptr<HandshakerFactory>, 2> factories_;
};

HandshakerFactoryList* g_handshaker_factory_lists = nullptr;

}  // namespace

void HandshakerFactoryList::Register(
    bool at_start, std::unique_ptr<HandshakerFactory> factory) {
  factories_.push_back(std::move(factory));
  if (at_start) {
    auto* end = &factories_[factories_.size() - 1];
    std::rotate(&factories_[0], end, end + 1);
  }
}

void HandshakerFactoryList::AddHandshakers(const grpc_channel_args* args,
                                           grpc_pollset_set* interested_parties,
                                           HandshakeManager* handshake_mgr) {
  for (size_t idx = 0; idx < factories_.size(); ++idx) {
    auto& handshaker_factory = factories_[idx];
    handshaker_factory->AddHandshakers(args, interested_parties, handshake_mgr);
  }
}

//
// plugin
//

void HandshakerRegistry::Init() {
  GPR_ASSERT(g_handshaker_factory_lists == nullptr);
  g_handshaker_factory_lists =
      static_cast<HandshakerFactoryList*>(gpr_malloc_aligned(
          sizeof(*g_handshaker_factory_lists) * NUM_HANDSHAKER_TYPES,
          GPR_MAX_ALIGNMENT));

  GPR_ASSERT(g_handshaker_factory_lists != nullptr);
  for (auto idx = 0; idx < NUM_HANDSHAKER_TYPES; ++idx) {
    auto factory_list = g_handshaker_factory_lists + idx;
    new (factory_list) HandshakerFactoryList();
  }
}

void HandshakerRegistry::Shutdown() {
  GPR_ASSERT(g_handshaker_factory_lists != nullptr);
  for (auto idx = 0; idx < NUM_HANDSHAKER_TYPES; ++idx) {
    auto factory_list = g_handshaker_factory_lists + idx;
    factory_list->~HandshakerFactoryList();
  }
  gpr_free_aligned(g_handshaker_factory_lists);
  g_handshaker_factory_lists = nullptr;
}

void HandshakerRegistry::RegisterHandshakerFactory(
    bool at_start, HandshakerType handshaker_type,
    std::unique_ptr<HandshakerFactory> factory) {
  GPR_ASSERT(g_handshaker_factory_lists != nullptr);
  auto& factory_list = g_handshaker_factory_lists[handshaker_type];
  factory_list.Register(at_start, std::move(factory));
}

void HandshakerRegistry::AddHandshakers(HandshakerType handshaker_type,
                                        const grpc_channel_args* args,
                                        grpc_pollset_set* interested_parties,
                                        HandshakeManager* handshake_mgr) {
  GPR_ASSERT(g_handshaker_factory_lists != nullptr);
  auto& factory_list = g_handshaker_factory_lists[handshaker_type];
  factory_list.AddHandshakers(args, interested_parties, handshake_mgr);
}

}  // namespace grpc_core
