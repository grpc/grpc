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

#include "src/core/tsi/gts_transport_security.h"

#include <string.h>

static gts_shared_resource g_gts_resource;

gts_shared_resource* gts_get_shared_resource(void) { return &g_gts_resource; }

extern "C" void grpc_tsi_gts_init() {
  memset(&g_gts_resource, 0, sizeof(gts_shared_resource));
  gpr_mu_init(&g_gts_resource.mu);
}

extern "C" void grpc_tsi_gts_shutdown() {
  gpr_mu_destroy(&g_gts_resource.mu);
  if (g_gts_resource.cq == nullptr) {
    return;
  }
  grpc_completion_queue_destroy(g_gts_resource.cq);
  grpc_channel_destroy(g_gts_resource.channel);
  gpr_thd_join(g_gts_resource.thread_id);
}
