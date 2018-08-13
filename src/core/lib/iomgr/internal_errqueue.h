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

/* This file contains constants defined in <linux/errqueue.h> and
 * <linux/net_tstamp.h> so as to allow collecting network timestamps in the
 * kernel. This file allows tcp_posix.cc to compile on platforms that do not
 * have <linux/errqueue.h> and <linux/net_tstamp.h>.
 */

#ifndef GRPC_CORE_LIB_IOMGR_INTERNAL_ERRQUEUE_H
#define GRPC_CORE_LIB_IOMGR_INTERNAL_ERRQUEUE_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include <sys/types.h>
#include <time.h>

#ifdef GRPC_LINUX_ERRQUEUE
#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <sys/socket.h>
#endif /* GRPC_LINUX_ERRQUEUE */

namespace grpc_core {
/* Returns true if kernel is capable of supporting errqueue and timestamping.
 * Currently allowing only linux kernels above 4.0.0
 */
bool kernel_supports_errqueue();
}  // namespace grpc_core

#endif /* GRPC_POSIX_SOCKET_TCP */

#endif /* GRPC_CORE_LIB_IOMGR_INTERNAL_ERRQUEUE_H */
