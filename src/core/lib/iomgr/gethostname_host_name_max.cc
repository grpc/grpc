/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/iomgr/gethostname.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_HOST_NAME_MAX

#include <limits.h>
#include <unistd.h>

#include <grpc/support/alloc.h>

char* grpc_gethostname() {
  char* hostname = static_cast<char*>(gpr_malloc(HOST_NAME_MAX));
  if (gethostname(hostname, HOST_NAME_MAX) != 0) {
    gpr_free(hostname);
    return nullptr;
  }
  return hostname;
}

#endif  // GRPC_POSIX_HOST_NAME_MAX
