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

#include <grpc/support/port_platform.h>

#include <string.h>

#include "src/core/tsi/alts_transport_security.h"

static alts_shared_resource g_alts_resource;

alts_shared_resource* grpc_alts_get_shared_resource(void) {
  return &g_alts_resource;
}

void grpc_tsi_alts_init() {
  g_alts_resource.channel = nullptr;
  gpr_mu_init(&g_alts_resource.mu);
  gpr_ref_init(&g_alts_resource.refcount, 0);
}

alts_shared_resource* grpc_alts_shared_resource_ref(
    alts_shared_resource* resource) {
  gpr_mu_lock(&resource->mu);
  gpr_ref(&resource->refcount);
  gpr_mu_unlock(&resource->mu);
}

void grpc_alts_shared_resource_unref(alts_shared_resource* resource) {
  gpr_mu_lock(&resource->mu);
  if (gpr_unref(&resource->refcount)) {
    grpc_channel_destroy(resource->channel);
    resource->channel = nullptr;
  }
  gpr_mu_unlock(&resource->mu);
}

void grpc_tsi_alts_shutdown() {
  GPR_ASSERT(g_alts_resource.channel == nullptr);
  gpr_mu_destroy(&g_alts_resource.mu);
}
