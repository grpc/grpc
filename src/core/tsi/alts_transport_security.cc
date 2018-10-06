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

#include "src/core/tsi/alts_transport_security.h"

#include <string.h>

static alts_shared_resource g_alts_resource;

alts_shared_resource* alts_get_shared_resource(void) {
  return &g_alts_resource;
}

void grpc_tsi_alts_init() {
  g_alts_resource.channel = nullptr;
  gpr_mu_init(&g_alts_resource.mu);
}

void grpc_tsi_alts_shutdown() {
  if (g_alts_resource.channel != nullptr) {
    grpc_channel_destroy(g_alts_resource.channel);
  }
  gpr_mu_destroy(&g_alts_resource.mu);
}
