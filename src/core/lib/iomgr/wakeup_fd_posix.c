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

#ifdef GRPC_POSIX_WAKEUP_FD

#include <stddef.h>
#include "src/core/lib/iomgr/wakeup_fd_cv.h"
#include "src/core/lib/iomgr/wakeup_fd_pipe.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

extern grpc_wakeup_fd_vtable grpc_cv_wakeup_fd_vtable;
static const grpc_wakeup_fd_vtable *wakeup_fd_vtable = NULL;

int grpc_allow_specialized_wakeup_fd = 1;
int grpc_allow_pipe_wakeup_fd = 1;

int has_real_wakeup_fd = 1;
int cv_wakeup_fds_enabled = 0;

void grpc_wakeup_fd_global_init(void) {
  if (grpc_allow_specialized_wakeup_fd &&
      grpc_specialized_wakeup_fd_vtable.check_availability()) {
    wakeup_fd_vtable = &grpc_specialized_wakeup_fd_vtable;
  } else if (grpc_allow_pipe_wakeup_fd &&
             grpc_pipe_wakeup_fd_vtable.check_availability()) {
    wakeup_fd_vtable = &grpc_pipe_wakeup_fd_vtable;
  } else {
    has_real_wakeup_fd = 0;
  }
}

void grpc_wakeup_fd_global_destroy(void) { wakeup_fd_vtable = NULL; }

int grpc_has_wakeup_fd(void) { return has_real_wakeup_fd; }

int grpc_cv_wakeup_fds_enabled(void) { return cv_wakeup_fds_enabled; }

void grpc_enable_cv_wakeup_fds(int enable) { cv_wakeup_fds_enabled = enable; }

grpc_error *grpc_wakeup_fd_init(grpc_wakeup_fd *fd_info) {
  if (cv_wakeup_fds_enabled) {
    return grpc_cv_wakeup_fd_vtable.init(fd_info);
  }
  return wakeup_fd_vtable->init(fd_info);
}

grpc_error *grpc_wakeup_fd_consume_wakeup(grpc_wakeup_fd *fd_info) {
  if (cv_wakeup_fds_enabled) {
    return grpc_cv_wakeup_fd_vtable.consume(fd_info);
  }
  return wakeup_fd_vtable->consume(fd_info);
}

grpc_error *grpc_wakeup_fd_wakeup(grpc_wakeup_fd *fd_info) {
  if (cv_wakeup_fds_enabled) {
    return grpc_cv_wakeup_fd_vtable.wakeup(fd_info);
  }
  return wakeup_fd_vtable->wakeup(fd_info);
}

void grpc_wakeup_fd_destroy(grpc_wakeup_fd *fd_info) {
  if (cv_wakeup_fds_enabled) {
    grpc_cv_wakeup_fd_vtable.destroy(fd_info);
  } else {
    wakeup_fd_vtable->destroy(fd_info);
  }
}

#endif /* GRPC_POSIX_WAKEUP_FD */
