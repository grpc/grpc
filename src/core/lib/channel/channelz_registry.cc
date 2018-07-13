/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <cstring>

namespace grpc_core {
namespace channelz {
namespace {

// singleton instance of the registry.
ChannelzRegistry* g_channelz_registry = nullptr;

}  // anonymous namespace

void ChannelzRegistry::Init() { g_channelz_registry = New<ChannelzRegistry>(); }

void ChannelzRegistry::Shutdown() { Delete(g_channelz_registry); }

ChannelzRegistry* ChannelzRegistry::Default() {
  GPR_DEBUG_ASSERT(g_channelz_registry != nullptr);
  return g_channelz_registry;
}

ChannelzRegistry::ChannelzRegistry() { gpr_mu_init(&mu_); }

ChannelzRegistry::~ChannelzRegistry() { gpr_mu_destroy(&mu_); }

intptr_t ChannelzRegistry::InternalRegisterEntry(const RegistryEntry& entry) {
  mu_guard guard(&mu_);
  entities_.push_back(entry);
  intptr_t uuid = entities_.size();
  return uuid;
}

void ChannelzRegistry::InternalUnregisterEntry(intptr_t uuid, EntityType type) {
  GPR_ASSERT(uuid >= 1);
  mu_guard guard(&mu_);
  GPR_ASSERT(static_cast<size_t>(uuid) <= entities_.size());
  GPR_ASSERT(entities_[uuid - 1].type == type);
  entities_[uuid - 1].object = nullptr;
}

void* ChannelzRegistry::InternalGetEntry(intptr_t uuid, EntityType type) {
  mu_guard guard(&mu_);
  if (uuid < 1 || uuid > static_cast<intptr_t>(entities_.size())) {
    return nullptr;
  }
  if (entities_[uuid - 1].type == type) {
    return entities_[uuid - 1].object;
  } else {
    return nullptr;
  }
}

}  // namespace channelz
}  // namespace grpc_core
