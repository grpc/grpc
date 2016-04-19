/*
 *
 * Copyright 2015, Google Inc.
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

#include "src/core/ext/client_config/lb_policy_registry.h"

#include <string.h>

#define MAX_POLICIES 10

static grpc_lb_policy_factory *g_all_of_the_lb_policies[MAX_POLICIES];
static int g_number_of_lb_policies = 0;

void grpc_lb_policy_registry_init(void) { g_number_of_lb_policies = 0; }

void grpc_lb_policy_registry_shutdown(void) {
  int i;
  for (i = 0; i < g_number_of_lb_policies; i++) {
    grpc_lb_policy_factory_unref(g_all_of_the_lb_policies[i]);
  }
}

void grpc_register_lb_policy(grpc_lb_policy_factory *factory) {
  int i;
  for (i = 0; i < g_number_of_lb_policies; i++) {
    GPR_ASSERT(0 != strcmp(factory->vtable->name,
                           g_all_of_the_lb_policies[i]->vtable->name));
  }
  GPR_ASSERT(g_number_of_lb_policies != MAX_POLICIES);
  grpc_lb_policy_factory_ref(factory);
  g_all_of_the_lb_policies[g_number_of_lb_policies++] = factory;
}

static grpc_lb_policy_factory *lookup_factory(const char *name) {
  int i;

  if (name == NULL) return NULL;

  for (i = 0; i < g_number_of_lb_policies; i++) {
    if (0 == strcmp(name, g_all_of_the_lb_policies[i]->vtable->name)) {
      return g_all_of_the_lb_policies[i];
    }
  }

  return NULL;
}

grpc_lb_policy *grpc_lb_policy_create(grpc_exec_ctx *exec_ctx, const char *name,
                                      grpc_lb_policy_args *args) {
  grpc_lb_policy_factory *factory = lookup_factory(name);
  grpc_lb_policy *lb_policy =
      grpc_lb_policy_factory_create_lb_policy(exec_ctx, factory, args);
  return lb_policy;
}
