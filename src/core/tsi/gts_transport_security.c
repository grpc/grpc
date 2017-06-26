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

#include <grpc/support/alloc.h>
#include "src/core/tsi/gts_transport_security.h"

static gts_shared_resource* gts_resource = NULL;

gts_shared_resource** gts_get_shared_resource(void) { return &gts_resource; }

void gts_destroy() {
  if (gts_resource == NULL) {
    return;
  }
  grpc_completion_queue_destroy(gts_resource->gts_cq);
  grpc_channel_destroy(gts_resource->gts_channel);
  gpr_thd_join(gts_resource->gts_thread_id);
  gpr_mu_destroy(&gts_resource->gts_mu);
  gpr_free(gts_resource);
}

