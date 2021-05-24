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

#include <stddef.h>

#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/timer_manager.h"

static grpc_iomgr_platform_vtable* iomgr_platform_vtable = nullptr;

void grpc_set_iomgr_platform_vtable(grpc_iomgr_platform_vtable* vtable) {
  iomgr_platform_vtable = vtable;
}

void grpc_determine_iomgr_platform() {
  if (iomgr_platform_vtable == nullptr) {
    grpc_set_default_iomgr_platform();
  }
}

void grpc_iomgr_platform_init() { iomgr_platform_vtable->init(); }

void grpc_iomgr_platform_flush() { iomgr_platform_vtable->flush(); }

void grpc_iomgr_platform_shutdown() { iomgr_platform_vtable->shutdown(); }

void grpc_iomgr_platform_shutdown_background_closure() {
  iomgr_platform_vtable->shutdown_background_closure();
}

bool grpc_iomgr_platform_is_any_background_poller_thread() {
  return iomgr_platform_vtable->is_any_background_poller_thread();
}

bool grpc_iomgr_platform_add_closure_to_background_poller(
    grpc_closure* closure, grpc_error_handle error) {
  return iomgr_platform_vtable->add_closure_to_background_poller(closure,
                                                                 error);
}
