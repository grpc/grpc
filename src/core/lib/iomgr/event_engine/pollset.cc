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

#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"

struct grpc_pollset {
  gpr_mu mu;
};

namespace {

// --- pollset vtable API ---
void pollset_global_init(void) {}
void pollset_global_shutdown(void) {}
void pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
}
void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  (void)pollset;
  (void)closure;
}
void pollset_destroy(grpc_pollset* pollset) { gpr_mu_destroy(&pollset->mu); }
grpc_error* pollset_work(grpc_pollset* pollset, grpc_pollset_worker** worker,
                         grpc_millis deadline) {
  (void)pollset;
  (void)worker;
  (void)deadline;
  return GRPC_ERROR_NONE;
}
grpc_error* pollset_kick(grpc_pollset* pollset,
                         grpc_pollset_worker* specific_worker) {
  (void)pollset;
  (void)specific_worker;
  return GRPC_ERROR_NONE;
}
size_t pollset_size(void) { return 0; }

// --- pollset_set vtable API ---
grpc_pollset_set* pollset_set_create(void) {
  return reinterpret_cast<grpc_pollset_set*>(static_cast<intptr_t>(0xBAAAAAAD));
}
void pollset_set_destroy(grpc_pollset_set* pollset_set) { (void)pollset_set; }
void pollset_set_add_pollset(grpc_pollset_set* pollset_set,
                             grpc_pollset* pollset) {
  (void)pollset_set;
  (void)pollset;
}
void pollset_set_del_pollset(grpc_pollset_set* pollset_set,
                             grpc_pollset* pollset) {
  (void)pollset_set;
  (void)pollset;
}
void pollset_set_add_pollset_set(grpc_pollset_set* bag,
                                 grpc_pollset_set* item) {
  (void)bag;
  (void)item;
}
void pollset_set_del_pollset_set(grpc_pollset_set* bag,
                                 grpc_pollset_set* item) {
  (void)bag;
  (void)item;
}

}  // namespace

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
