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
#if defined(GRPC_EVENT_ENGINE_TEST)

#include <unordered_set>

#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"

struct grpc_pollset {
  gpr_mu mu;
  gpr_cv cv;
};

struct grpc_pollset_set {
  gpr_mu mu;
  std::unordered_set<grpc_pollset*> set;
};


namespace {

// --- pollset vtable API ---
void pollset_global_init(void) {}
void pollset_global_shutdown(void) {}
void pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  gpr_mu_init(&pollset->mu);
  gpr_cv_init(&pollset->cv);
  *mu = &pollset->mu;
}
void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
}
void pollset_destroy(grpc_pollset* pollset) {
  gpr_mu_destroy(&pollset->mu);
  gpr_cv_destroy(&pollset->cv);
}
grpc_error* pollset_work(grpc_pollset* pollset, grpc_pollset_worker** worker,
                         grpc_millis deadline) {
  (void)worker;
  gpr_cv_wait(&pollset->cv, &pollset->mu,
              grpc_millis_to_timespec(deadline, GPR_CLOCK_REALTIME));
  return GRPC_ERROR_NONE;
}
grpc_error* pollset_kick(grpc_pollset* pollset,
                         grpc_pollset_worker* specific_worker) {
  (void)pollset;
  (void)specific_worker;
  return GRPC_ERROR_NONE;
}
size_t pollset_size(void) { return sizeof(grpc_pollset); }

// --- pollset_set vtable API ---
grpc_pollset_set* pollset_set_create(void) {
  grpc_pollset_set* ret = new grpc_pollset_set();
  gpr_mu_init(&ret->mu);
  return ret;
}
void pollset_set_destroy(grpc_pollset_set* pollset_set) {
  gpr_mu_destroy(&pollset_set->mu);
  delete pollset_set;
}
void pollset_set_add_pollset(grpc_pollset_set* pollset_set,
                             grpc_pollset* pollset) {
  gpr_mu_lock(&pollset_set->mu);
  pollset_set->set.emplace(pollset);
  gpr_mu_unlock(&pollset_set->mu);
}
void pollset_set_del_pollset(grpc_pollset_set* pollset_set,
                             grpc_pollset* pollset) {
  gpr_mu_lock(&pollset_set->mu);
  auto it = pollset_set->set.find(pollset);
  if (it != pollset_set->set.end()) {
    pollset_set->set.erase(it);
  }
  gpr_mu_unlock(&pollset_set->mu);
}
void pollset_set_add_pollset_set(grpc_pollset_set* bag,
                                 grpc_pollset_set* item) {
  (void)bag;
  (void)item;
  abort();
}
void pollset_set_del_pollset_set(grpc_pollset_set* bag,
                                 grpc_pollset_set* item) {
  (void)bag;
  (void)item;
  abort();
}

}  // namespace

void pollset_ee_broadcast_event(grpc_pollset_set* set) {
  gpr_mu_lock(&set->mu);
  for (auto& it : set->set) {
    gpr_cv_signal(&it->cv);
  }
  gpr_mu_unlock(&set->mu);
}

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

#endif  // GRPC_EVENT_ENGINE_TEST
