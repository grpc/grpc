/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SUBPROCESS

#include <grpc/support/subprocess.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

struct gpr_subprocess {
  int pid;
  int joined;
};

const char *gpr_subprocess_binary_extension() { return ""; }

gpr_subprocess *gpr_subprocess_create(int argc, const char **argv) {
  gpr_subprocess *r;
  int pid;
  char **exec_args;

  pid = fork();
  if (pid == -1) {
    return NULL;
  } else if (pid == 0) {
    exec_args = gpr_malloc(((size_t)argc + 1) * sizeof(char *));
    memcpy(exec_args, argv, (size_t)argc * sizeof(char *));
    exec_args[argc] = NULL;
    execv(exec_args[0], exec_args);
    /* if we reach here, an error has occurred */
    gpr_log(GPR_ERROR, "execv '%s' failed: %s", exec_args[0], strerror(errno));
    _exit(1);
    return NULL;
  } else {
    r = gpr_malloc(sizeof(gpr_subprocess));
    memset(r, 0, sizeof(*r));
    r->pid = pid;
    return r;
  }
}

void gpr_subprocess_destroy(gpr_subprocess *p) {
  if (!p->joined) {
    kill(p->pid, SIGKILL);
    gpr_subprocess_join(p);
  }
  gpr_free(p);
}

int gpr_subprocess_join(gpr_subprocess *p) {
  int status;
retry:
  if (waitpid(p->pid, &status, 0) == -1) {
    if (errno == EINTR) {
      goto retry;
    }
    gpr_log(GPR_ERROR, "waitpid failed: %s", strerror(errno));
    return -1;
  }
  return status;
}

void gpr_subprocess_interrupt(gpr_subprocess *p) {
  if (!p->joined) {
    kill(p->pid, SIGINT);
  }
}

#endif /* GPR_POSIX_SUBPROCESS */
