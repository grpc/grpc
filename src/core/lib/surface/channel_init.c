/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
    g_slots[type].slots =
        gpr_realloc(g_slots[type].slots,
                    g_slots[type].cap_slots * sizeof(*g_slots[type].slots));
  }
  stage_slot *s = &g_slots[type].slots[g_slots[type].num_slots++];
  s->insertion_order = g_slots[type].num_slots;
  s->priority = priority;
  s->fn = stage;
  s->arg = stage_arg;
}

static int compare_slots(const void *a, const void *b) {
  const stage_slot *sa = a;
  const stage_slot *sb = b;

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
    g_slots[i].slots = (void *)(uintptr_t)0xdeadbeef;
  }
}

static const char *name_for_type(grpc_channel_stack_type type) {
  switch (type) {
    case GRPC_CLIENT_CHANNEL:
      return "CLIENT_CHANNEL";
    case GRPC_CLIENT_SUBCHANNEL:
      return "CLIENT_SUBCHANNEL";
    case GRPC_SERVER_CHANNEL:
      return "SERVER_CHANNEL";
    case GRPC_CLIENT_LAME_CHANNEL:
      return "CLIENT_LAME_CHANNEL";
    case GRPC_CLIENT_DIRECT_CHANNEL:
      return "CLIENT_DIRECT_CHANNEL";
    case GRPC_NUM_CHANNEL_STACK_TYPES:
      break;
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

bool grpc_channel_init_create_stack(grpc_exec_ctx *exec_ctx,
                                    grpc_channel_stack_builder *builder,
                                    grpc_channel_stack_type type) {
  GPR_ASSERT(g_finalized);

  grpc_channel_stack_builder_set_name(builder, name_for_type(type));

  for (size_t i = 0; i < g_slots[type].num_slots; i++) {
    const stage_slot *slot = &g_slots[type].slots[i];
    if (!slot->fn(exec_ctx, builder, slot->arg)) {
      return false;
    }
  }

  return true;
}
