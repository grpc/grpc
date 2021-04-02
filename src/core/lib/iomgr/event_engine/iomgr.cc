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

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/timer.h"

extern grpc_tcp_client_vtable grpc_event_engine_tcp_client_vtable;
extern grpc_tcp_server_vtable grpc_event_engine_tcp_server_vtable;
extern grpc_timer_vtable grpc_event_engine_timer_vtable;
extern grpc_pollset_vtable grpc_event_engine_pollset_vtable;
extern grpc_pollset_set_vtable grpc_event_engine_pollset_set_vtable;
extern grpc_address_resolver_vtable grpc_event_engine_resolver_vtable;

// TODO(hork): temporary
extern grpc_timer_vtable grpc_generic_timer_vtable;

static void iomgr_platform_init(void) {
  // The default EventEngine is lazily instantiated via
  // `grpc_get_default_event_engine`
}
static void iomgr_platform_flush(void) {
  // EventEngine::Flush??
}
static void iomgr_platform_shutdown(void) {}
static void iomgr_platform_shutdown_background_closure(void) {}
static bool iomgr_platform_is_any_background_poller_thread(void) {
  return false;
}
static bool iomgr_platform_add_closure_to_background_poller(
    grpc_closure* closure, grpc_error* error) {
  (void)closure;
  (void)error;
  return false;
}

static grpc_iomgr_platform_vtable vtable = {
    iomgr_platform_init,
    iomgr_platform_flush,
    iomgr_platform_shutdown,
    iomgr_platform_shutdown_background_closure,
    iomgr_platform_is_any_background_poller_thread,
    iomgr_platform_add_closure_to_background_poller};

void grpc_set_default_iomgr_platform() {
  grpc_set_tcp_client_impl(&grpc_event_engine_tcp_client_vtable);
  grpc_set_tcp_server_impl(&grpc_event_engine_tcp_server_vtable);
  grpc_set_timer_impl(&grpc_generic_timer_vtable);
  grpc_set_pollset_vtable(&grpc_event_engine_pollset_vtable);
  grpc_set_pollset_set_vtable(&grpc_event_engine_pollset_set_vtable);
  grpc_set_resolver_impl(&grpc_event_engine_resolver_vtable);
  grpc_set_iomgr_platform_vtable(&vtable);
}

#endif
