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

#include <grpc/impl/codegen/log.h>
#include "src/core/lib/iomgr/internal_errqueue.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

namespace grpc_core {
static bool errqueue_supported = false;

bool kernel_supports_errqueue() { return errqueue_supported; }

void grpc_errqueue_init() {
/* Both-compile time and run-time linux kernel versions should be atleast 4.0.0
 */
#ifdef GRPC_LINUX_ERRQUEUE
  struct utsname buffer;
  if (uname(&buffer) != 0) {
    gpr_log(GPR_ERROR, "uname: %s", strerror(errno));
    return;
  }
  char* release = buffer.release;
  if (release == nullptr) {
    return;
  }

  if (strtol(release, nullptr, 10) >= 4) {
    errqueue_supported = true;
  } else {
    gpr_log(GPR_DEBUG, "ERRQUEUE support not enabled");
  }
#endif /* GRPC_LINUX_ERRQUEUE */
}
} /* namespace grpc_core */

#else

namespace grpc_core {
void grpc_errqueue_init() {}
} /* namespace grpc_core */

#endif /* GRPC_POSIX_SOCKET_TCP */
