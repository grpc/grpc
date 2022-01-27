// Copyright 2021 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <grpc/support/port_platform.h>

#ifdef GRPC_USE_EVENT_ENGINE
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/event_engine/pollset.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"

namespace {

static gpr_mu g_mu;
static gpr_cv g_cv;

// --- pollset vtable API ---
void pollset_global_init(void) {
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv);
}
void pollset_global_shutdown(void) {
  gpr_cv_destroy(&g_cv);
  gpr_mu_destroy(&g_mu);
}
void pollset_init(grpc_pollset* pollset, gpr_mu** mu) { *mu = &g_mu; }
void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
}
void pollset_destroy(grpc_pollset* pollset) {}
grpc_error_handle pollset_work(grpc_pollset* pollset,
                               grpc_pollset_worker** worker,
                               grpc_millis deadline) {
  (void)worker;
  gpr_cv_wait(&g_cv, &g_mu,
              grpc_millis_to_timespec(deadline, GPR_CLOCK_REALTIME));
  return GRPC_ERROR_NONE;
}
grpc_error_handle pollset_kick(grpc_pollset* pollset,
                               grpc_pollset_worker* specific_worker) {
  (void)pollset;
  (void)specific_worker;
  return GRPC_ERROR_NONE;
}
size_t pollset_size(void) { return 1; }

// --- pollset_set vtable API ---
grpc_pollset_set* pollset_set_create(void) { return nullptr; }
void pollset_set_destroy(grpc_pollset_set* pollset_set) {}
void pollset_set_add_pollset(grpc_pollset_set* pollset_set,
                             grpc_pollset* pollset) {}

void pollset_set_del_pollset(grpc_pollset_set* pollset_set,
                             grpc_pollset* pollset) {}
void pollset_set_add_pollset_set(grpc_pollset_set* bag,
                                 grpc_pollset_set* item) {}
void pollset_set_del_pollset_set(grpc_pollset_set* bag,
                                 grpc_pollset_set* item) {}

}  // namespace

void grpc_pollset_ee_broadcast_event() { gpr_cv_signal(&g_cv); }

// --- vtables ---
grpc_pollset_vtable grpc_event_engine_pollset_vtable = {
    pollset_global_init, pollset_global_shutdown,
    pollset_init,        pollset_shutdown,
    pollset_destroy,     pollset_work,
    pollset_kick,        pollset_size};

grpc_pollset_set_vtable grpc_event_engine_pollset_set_vtable = {
    pollset_set_create,          pollset_set_destroy,
    pollset_set_add_pollset,     pollset_set_del_pollset,
    pollset_set_add_pollset_set, pollset_set_del_pollset_set};

#endif  // GRPC_USE_EVENT_ENGINE
