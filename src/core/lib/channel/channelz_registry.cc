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

namespace grpc_core {
namespace {

// singleton instance of the registry.
ChannelzRegistry* g_channelz_registry = nullptr;

// avl vtable for uuid (intptr_t) -> channelz_obj (void*)
// this table is only looking, it does not own anything.
void destroy_intptr(void* not_used, void* user_data) {}
void* copy_intptr(void* key, void* user_data) { return key; }
long compare_intptr(void* key1, void* key2, void* user_data) {
  return GPR_ICMP(key1, key2);
}

void destroy_channelz_obj(void* channelz_obj, void* user_data) {}
void* copy_channelz_obj(void* channelz_obj, void* user_data) {
  return channelz_obj;
}
const grpc_avl_vtable avl_vtable = {destroy_intptr, copy_intptr, compare_intptr,
                                    destroy_channelz_obj, copy_channelz_obj};

}  // anonymous namespace

void ChannelzRegistry::Init() { g_channelz_registry = New<ChannelzRegistry>(); }

void ChannelzRegistry::Shutdown() { Delete(g_channelz_registry); }

ChannelzRegistry* ChannelzRegistry::Default() {
  GPR_DEBUG_ASSERT(g_channelz_registry != nullptr);
  return g_channelz_registry;
}

ChannelzRegistry::ChannelzRegistry() : uuid_(1) {
  gpr_mu_init(&mu_);
  avl_ = grpc_avl_create(&avl_vtable);
}

ChannelzRegistry::~ChannelzRegistry() {
  grpc_avl_unref(avl_, nullptr);
  gpr_mu_destroy(&mu_);
}

void ChannelzRegistry::InternalUnregister(intptr_t uuid) {
  gpr_mu_lock(&mu_);
  avl_ = grpc_avl_remove(avl_, (void*)uuid, nullptr);
  gpr_mu_unlock(&mu_);
}

}  // namespace grpc_core
