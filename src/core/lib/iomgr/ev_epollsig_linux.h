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

#ifndef GRPC_CORE_LIB_IOMGR_EV_EPOLLSIG_LINUX_H
#define GRPC_CORE_LIB_IOMGR_EV_EPOLLSIG_LINUX_H

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/port.h"

const grpc_event_engine_vtable* grpc_init_epollsig_linux(bool explicit_request);

#ifdef GRPC_LINUX_EPOLL_CREATE1
void* grpc_fd_get_polling_island(grpc_fd* fd);
void* grpc_pollset_get_polling_island(grpc_pollset* ps);
bool grpc_are_polling_islands_equal(void* p, void* q);
#endif /* defined(GRPC_LINUX_EPOLL_CREATE1) */

#endif /* GRPC_CORE_LIB_IOMGR_EV_EPOLLSIG_LINUX_H */
