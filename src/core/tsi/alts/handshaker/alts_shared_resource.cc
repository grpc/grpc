/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/tsi/alts/handshaker/alts_shared_resource.h"

#include <grpc/support/log.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_event.h"

static alts_shared_resource_dedicated g_alts_resource_dedicated;
static alts_shared_resource* kSharedResource = alts_get_shared_resource();

alts_shared_resource_dedicated* grpc_alts_get_shared_resource_dedicated(void) {
  return &g_alts_resource_dedicated;
}

static void wait_for_cq_drain() {
  gpr_mu_lock(&kSharedResource->mu);
  while (!g_alts_resource_dedicated.is_cq_drained) {
    gpr_cv_wait(&g_alts_resource_dedicated.cv, &kSharedResource->mu,
                gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&kSharedResource->mu);
}

static void signal_for_cq_destroy() {
  gpr_mu_lock(&kSharedResource->mu);
  g_alts_resource_dedicated.is_cq_drained = true;
  gpr_cv_signal(&g_alts_resource_dedicated.cv);
  gpr_mu_unlock(&kSharedResource->mu);
}

static void thread_worker(void* arg) {
  while (true) {
    grpc_event event =
        grpc_completion_queue_next(g_alts_resource_dedicated.cq,
                                   gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(event.type != GRPC_QUEUE_TIMEOUT);
    if (event.type == GRPC_QUEUE_SHUTDOWN) {
      signal_for_cq_destroy();
      break;
    }
    /* event.type == GRPC_OP_COMPLETE. */
    alts_tsi_event* alts_event = static_cast<alts_tsi_event*>(event.tag);
    alts_tsi_event_dispatch_to_handshaker(alts_event, event.success);
    alts_tsi_event_destroy(alts_event);
  }
}

void grpc_alts_shared_resource_dedicated_init() {
  gpr_cv_init(&g_alts_resource_dedicated.cv);
  g_alts_resource_dedicated.cq = nullptr;
}

void grpc_alts_shared_resource_dedicated_populate() {
  g_alts_resource_dedicated.cq = grpc_completion_queue_create_for_next(nullptr);
  g_alts_resource_dedicated.thread =
      grpc_core::Thread("alts_tsi_handshaker", &thread_worker, nullptr);
  g_alts_resource_dedicated.thread.Start();
}

void grpc_alts_shared_resource_dedicated_shutdown() {
  if (g_alts_resource_dedicated.cq != nullptr) {
    grpc_completion_queue_shutdown(g_alts_resource_dedicated.cq);
    wait_for_cq_drain();
    grpc_completion_queue_destroy(g_alts_resource_dedicated.cq);
    g_alts_resource_dedicated.thread.Join();
  }
  gpr_cv_destroy(&g_alts_resource_dedicated.cv);
}
