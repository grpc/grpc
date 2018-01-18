/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"

#include <string.h>

#include "src/core/lib/gpr/string.h"

#define MAX_POLICIES 10

static grpc_lb_policy_factory* g_all_of_the_lb_policies[MAX_POLICIES];
static int g_number_of_lb_policies = 0;

void grpc_lb_policy_registry_init(void) { g_number_of_lb_policies = 0; }

void grpc_lb_policy_registry_shutdown(void) {
  int i;
  for (i = 0; i < g_number_of_lb_policies; i++) {
    grpc_lb_policy_factory_unref(g_all_of_the_lb_policies[i]);
  }
}

void grpc_register_lb_policy(grpc_lb_policy_factory* factory) {
  int i;
  for (i = 0; i < g_number_of_lb_policies; i++) {
    GPR_ASSERT(0 != gpr_stricmp(factory->vtable->name,
                                g_all_of_the_lb_policies[i]->vtable->name));
  }
  GPR_ASSERT(g_number_of_lb_policies != MAX_POLICIES);
  grpc_lb_policy_factory_ref(factory);
  g_all_of_the_lb_policies[g_number_of_lb_policies++] = factory;
}

static grpc_lb_policy_factory* lookup_factory(const char* name) {
  int i;

  if (name == nullptr) return nullptr;

  for (i = 0; i < g_number_of_lb_policies; i++) {
    if (0 == gpr_stricmp(name, g_all_of_the_lb_policies[i]->vtable->name)) {
      return g_all_of_the_lb_policies[i];
    }
  }

  return nullptr;
}

grpc_lb_policy* grpc_lb_policy_create(const char* name,
                                      grpc_lb_policy_args* args) {
  grpc_lb_policy_factory* factory = lookup_factory(name);
  grpc_lb_policy* lb_policy =
      grpc_lb_policy_factory_create_lb_policy(factory, args);
  return lb_policy;
}
