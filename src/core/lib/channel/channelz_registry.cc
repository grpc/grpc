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

#include <cstring>

namespace grpc_core {
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

void ChannelzRegistry::InternalUnregister(intptr_t uuid) {
  GPR_ASSERT(uuid >= 1);
  gpr_mu_lock(&mu_);
  GPR_ASSERT(static_cast<size_t>(uuid) <= entities_.size());
  entities_[uuid - 1] = nullptr;
  gpr_mu_unlock(&mu_);
}

}  // namespace grpc_core
