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

#include "src/core/lib/surface/channel_init.h"

#include <grpc/support/alloc.h>
#include <grpc/support/useful.h>

typedef struct stage_slot {
  grpc_channel_init_stage fn;
  void *arg;
  int priority;
  size_t insertion_order;
} stage_slot;

typedef struct stage_slots {
  stage_slot *slots;
  size_t num_slots;
  size_t cap_slots;
} stage_slots;

static stage_slots g_slots[GRPC_NUM_CHANNEL_STACK_TYPES];
static bool g_finalized;

void grpc_channel_init_init(void) {
  for (int i = 0; i < GRPC_NUM_CHANNEL_STACK_TYPES; i++) {
    g_slots[i].slots = NULL;
    g_slots[i].num_slots = 0;
    g_slots[i].cap_slots = 0;
  }
  g_finalized = false;
}

void grpc_channel_init_register_stage(grpc_channel_stack_type type,
                                      int priority,
                                      grpc_channel_init_stage stage,
                                      void *stage_arg) {
  GPR_ASSERT(!g_finalized);
  if (g_slots[type].cap_slots == g_slots[type].num_slots) {
    g_slots[type].cap_slots = GPR_MAX(8, 3 * g_slots[type].cap_slots / 2);
    g_slots[type].slots = (stage_slot *)gpr_realloc(
        g_slots[type].slots,
        g_slots[type].cap_slots * sizeof(*g_slots[type].slots));
  }
  stage_slot *s = &g_slots[type].slots[g_slots[type].num_slots++];
  s->insertion_order = g_slots[type].num_slots;
  s->priority = priority;
  s->fn = stage;
  s->arg = stage_arg;
}

static int compare_slots(const void *a, const void *b) {
  const stage_slot *sa = (const stage_slot *)a;
  const stage_slot *sb = (const stage_slot *)b;

  int c = GPR_ICMP(sa->priority, sb->priority);
  if (c != 0) return c;
  return GPR_ICMP(sa->insertion_order, sb->insertion_order);
}

void grpc_channel_init_finalize(void) {
  GPR_ASSERT(!g_finalized);
  for (int i = 0; i < GRPC_NUM_CHANNEL_STACK_TYPES; i++) {
    qsort(g_slots[i].slots, g_slots[i].num_slots, sizeof(*g_slots[i].slots),
          compare_slots);
  }
  g_finalized = true;
}

void grpc_channel_init_shutdown(void) {
  for (int i = 0; i < GRPC_NUM_CHANNEL_STACK_TYPES; i++) {
    gpr_free(g_slots[i].slots);
    g_slots[i].slots = (stage_slot *)(void *)(uintptr_t)0xdeadbeef;
  }
}

bool grpc_channel_init_create_stack(grpc_exec_ctx *exec_ctx,
                                    grpc_channel_stack_builder *builder,
                                    grpc_channel_stack_type type) {
  GPR_ASSERT(g_finalized);

  grpc_channel_stack_builder_set_name(builder,
                                      grpc_channel_stack_type_string(type));

  for (size_t i = 0; i < g_slots[type].num_slots; i++) {
    const stage_slot *slot = &g_slots[type].slots[i];
    if (!slot->fn(exec_ctx, builder, slot->arg)) {
      return false;
    }
  }

  return true;
}
