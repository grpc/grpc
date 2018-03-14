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

#include "src/core/lib/iomgr/port.h"

#if defined(GRPC_CUSTOM_SOCKET) && defined(GRPC_UV)

#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset_custom.h"
#include "src/core/lib/iomgr/tcp_custom.h"
#include "src/core/lib/iomgr/timer_custom.h"

extern grpc_socket_vtable grpc_uv_socket_vtable;
extern grpc_custom_resolver_vtable uv_resolver_vtable;
extern grpc_custom_timer_vtable uv_timer_vtable;
extern grpc_custom_poller_vtable uv_pollset_vtable;

void grpc_set_default_iomgr_platform() {
  grpc_custom_iomgr_init(&grpc_uv_socket_vtable, &uv_resolver_vtable,
                         &uv_timer_vtable, &uv_pollset_vtable);
}
#endif
