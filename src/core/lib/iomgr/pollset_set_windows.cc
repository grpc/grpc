//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>
#include <stdint.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/pollset_set_windows.h"

static grpc_pollset_set* pollset_set_create(void) {
  return (grpc_pollset_set*)((intptr_t)0xdeafbeef);
}

static void pollset_set_destroy(grpc_pollset_set* /* pollset_set */) {}

static void pollset_set_add_pollset(grpc_pollset_set* /* pollset_set */,
                                    grpc_pollset* /* pollset */) {}

static void pollset_set_del_pollset(grpc_pollset_set* /* pollset_set */,
                                    grpc_pollset* /* pollset */) {}

static void pollset_set_add_pollset_set(grpc_pollset_set* /* bag */,
                                        grpc_pollset_set* /* item */) {}

static void pollset_set_del_pollset_set(grpc_pollset_set* /* bag */,
                                        grpc_pollset_set* /* item */) {}

grpc_pollset_set_vtable grpc_windows_pollset_set_vtable = {
    pollset_set_create,          pollset_set_destroy,
    pollset_set_add_pollset,     pollset_set_del_pollset,
    pollset_set_add_pollset_set, pollset_set_del_pollset_set};

#endif  // GRPC_WINSOCK_SOCKET
