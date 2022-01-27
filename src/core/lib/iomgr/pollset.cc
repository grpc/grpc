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

#include "src/core/lib/iomgr/pollset.h"

grpc_pollset_vtable* grpc_pollset_impl;

void grpc_set_pollset_vtable(grpc_pollset_vtable* vtable) {
  grpc_pollset_impl = vtable;
}

void grpc_pollset_global_init() { grpc_pollset_impl->global_init(); }

void grpc_pollset_global_shutdown() { grpc_pollset_impl->global_shutdown(); }

void grpc_pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  grpc_pollset_impl->init(pollset, mu);
}

void grpc_pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  grpc_pollset_impl->shutdown(pollset, closure);
}

void grpc_pollset_destroy(grpc_pollset* pollset) {
  grpc_pollset_impl->destroy(pollset);
}

grpc_error_handle grpc_pollset_work(grpc_pollset* pollset,
                                    grpc_pollset_worker** worker,
                                    grpc_millis deadline) {
  return grpc_pollset_impl->work(pollset, worker, deadline);
}

grpc_error_handle grpc_pollset_kick(grpc_pollset* pollset,
                                    grpc_pollset_worker* specific_worker) {
  return grpc_pollset_impl->kick(pollset, specific_worker);
}

size_t grpc_pollset_size(void) { return grpc_pollset_impl->pollset_size(); }
