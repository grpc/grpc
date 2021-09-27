/*
 *
 * Copyright 2016 gRPC authors.
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

#if GRPC_IF_NAMETOINDEX == 1 && defined(GRPC_POSIX_SOCKET_IF_NAMETOINDEX)

#include <errno.h>
#include <net/if.h>

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/grpc_if_nametoindex.h"

uint32_t grpc_if_nametoindex(char* name) {
  uint32_t out = if_nametoindex(name);
  if (out == 0) {
    gpr_log(GPR_DEBUG, "if_nametoindex failed for name %s. errno %d", name,
            errno);
  }
  return out;
}

#endif /* GRPC_IF_NAMETOINDEX == 1 && \
          defined(GRPC_POSIX_SOCKET_IF_NAMETOINDEX) */
