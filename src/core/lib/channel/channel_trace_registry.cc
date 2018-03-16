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

#include "src/core/lib/avl/avl.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channel_trace_registry.h"
#include "src/core/lib/gpr/useful.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

// file global lock and avl.
static gpr_mu g_mu;
static grpc_avl g_avl;
static gpr_atm g_uuid = 0;

// avl vtable for uuid (intptr_t) -> ChannelTrace
// this table is only looking, it does not own anything.
static void destroy_intptr(void* not_used, void* user_data) {}
static void* copy_intptr(void* key, void* user_data) { return key; }
static long compare_intptr(void* key1, void* key2, void* user_data) {
  return GPR_ICMP(key1, key2);
}

static void destroy_channel_trace(void* trace, void* user_data) {}
static void* copy_channel_trace(void* trace, void* user_data) { return trace; }
static const grpc_avl_vtable avl_vtable = {
    destroy_intptr, copy_intptr, compare_intptr, destroy_channel_trace,
    copy_channel_trace};

void grpc_channel_trace_registry_init() {
  gpr_mu_init(&g_mu);
  g_avl = grpc_avl_create(&avl_vtable);
}

void grpc_channel_trace_registry_shutdown() {
  grpc_avl_unref(g_avl, nullptr);
  gpr_mu_destroy(&g_mu);
}

intptr_t grpc_channel_trace_registry_register_channel_trace(
    grpc_core::ChannelTrace* channel_trace) {
  intptr_t prior = gpr_atm_no_barrier_fetch_add(&g_uuid, 1);
  gpr_mu_lock(&g_mu);
  g_avl = grpc_avl_add(g_avl, (void*)prior, channel_trace, nullptr);
  gpr_mu_unlock(&g_mu);
  return prior;
}

void grpc_channel_trace_registry_unregister_channel_trace(intptr_t uuid) {
  gpr_mu_lock(&g_mu);
  g_avl = grpc_avl_remove(g_avl, (void*)uuid, nullptr);
  gpr_mu_unlock(&g_mu);
}

grpc_core::ChannelTrace* grpc_channel_trace_registry_get_channel_trace(
    intptr_t uuid) {
  gpr_mu_lock(&g_mu);
  grpc_core::ChannelTrace* ret = static_cast<grpc_core::ChannelTrace*>(
      grpc_avl_get(g_avl, (void*)uuid, nullptr));
  gpr_mu_unlock(&g_mu);
  return ret;
}
