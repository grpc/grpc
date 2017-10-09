/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"

void grpc_iomgr_platform_init(void) {
  grpc_wakeup_fd_global_init();
  grpc_event_engine_init();
  grpc_register_tracer(&grpc_tcp_trace);
}

void grpc_iomgr_platform_flush(void) {}

void grpc_iomgr_platform_shutdown(void) {
  grpc_event_engine_shutdown();
  grpc_wakeup_fd_global_destroy();
}

#endif /* GRPC_POSIX_SOCKET */
