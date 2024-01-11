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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_WAKEUP_FD

#include <stddef.h>

#include "src/core/lib/iomgr/wakeup_fd_pipe.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

static const grpc_wakeup_fd_vtable* wakeup_fd_vtable = nullptr;

int grpc_allow_specialized_wakeup_fd = 1;
int grpc_allow_pipe_wakeup_fd = 1;

static int has_real_wakeup_fd = 1;

static gpr_once once_init_wakeup_fd = GPR_ONCE_INIT;

void grpc_wakeup_fd_global_init(void) {
  gpr_once_init(&once_init_wakeup_fd, []() {
    if (grpc_allow_specialized_wakeup_fd &&
        grpc_specialized_wakeup_fd_vtable.check_availability()) {
      wakeup_fd_vtable = &grpc_specialized_wakeup_fd_vtable;
    } else if (grpc_allow_pipe_wakeup_fd &&
               grpc_pipe_wakeup_fd_vtable.check_availability()) {
      wakeup_fd_vtable = &grpc_pipe_wakeup_fd_vtable;
    } else {
      has_real_wakeup_fd = 0;
    }
  });
}

void grpc_wakeup_fd_global_destroy(void) {}

int grpc_has_wakeup_fd(void) { return has_real_wakeup_fd; }

grpc_error_handle grpc_wakeup_fd_init(grpc_wakeup_fd* fd_info) {
  return wakeup_fd_vtable->init(fd_info);
}

grpc_error_handle grpc_wakeup_fd_consume_wakeup(grpc_wakeup_fd* fd_info) {
  return wakeup_fd_vtable->consume(fd_info);
}

grpc_error_handle grpc_wakeup_fd_wakeup(grpc_wakeup_fd* fd_info) {
  return wakeup_fd_vtable->wakeup(fd_info);
}

void grpc_wakeup_fd_destroy(grpc_wakeup_fd* fd_info) {
  wakeup_fd_vtable->destroy(fd_info);
}

#endif  // GRPC_POSIX_WAKEUP_FD
