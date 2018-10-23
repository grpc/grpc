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

#ifdef GRPC_LINUX_ERRQUEUE

/* Redefining scm_timestamping in the same way that <linux/errqueue.h> defines
 * it, so that code compiles on systems that don't have it. */
struct scm_timestamping {
  struct timespec ts[3];
};
/* Also redefine timestamp types */
/* The timestamp type for when the driver passed skb to NIC, or HW. */
constexpr int SCM_TSTAMP_SND = 0;
/* The timestamp type for when data entered the packet scheduler. */
constexpr int SCM_TSTAMP_SCHED = 1;
/* The timestamp type for when data acknowledged by peer. */
constexpr int SCM_TSTAMP_ACK = 2;
/* Redefine required constants from <linux/net_tstamp.h> */
constexpr uint32_t SOF_TIMESTAMPING_TX_SOFTWARE = 1u << 1;
constexpr uint32_t SOF_TIMESTAMPING_SOFTWARE = 1u << 4;
constexpr uint32_t SOF_TIMESTAMPING_OPT_ID = 1u << 7;
constexpr uint32_t SOF_TIMESTAMPING_TX_SCHED = 1u << 8;
constexpr uint32_t SOF_TIMESTAMPING_TX_ACK = 1u << 9;
constexpr uint32_t SOF_TIMESTAMPING_OPT_TSONLY = 1u << 11;

constexpr uint32_t kTimestampingSocketOptions = SOF_TIMESTAMPING_SOFTWARE |
                                                SOF_TIMESTAMPING_OPT_ID |
                                                SOF_TIMESTAMPING_OPT_TSONLY;
constexpr uint32_t kTimestampingRecordingOptions =
    SOF_TIMESTAMPING_TX_SCHED | SOF_TIMESTAMPING_TX_SOFTWARE |
    SOF_TIMESTAMPING_TX_ACK;
#endif /* GRPC_LINUX_ERRQUEUE */

/* Returns true if kernel is capable of supporting errqueue and timestamping.
 * Currently allowing only linux kernels above 4.0.0
 */
bool kernel_supports_errqueue();
}  // namespace grpc_core

#endif /* GRPC_POSIX_SOCKET_TCP */

#endif /* GRPC_CORE_LIB_IOMGR_INTERNAL_ERRQUEUE_H */
