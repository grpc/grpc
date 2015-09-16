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

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/util/port.h"

extern "C" {
#include "src/core/iomgr/socket_utils_posix.h"
#include "src/core/support/string.h"
}


int test_client(const char* root, int port) {
  int status;
  pid_t cli;
  cli = fork();
  if (cli == 0) {
    char* binary_path;
    char* port_arg;
    gpr_asprintf(&binary_path, "%s/../../examples/cpp/helloworld/greeter_client", root);
    gpr_asprintf(&port_arg, "%d", port);

    execl(binary_path, binary_path, port_arg, NULL);

    gpr_free(binary_path);
    gpr_free(port_arg);
    return 1;
  }
  /* wait for client */
  gpr_log(GPR_INFO, "Waiting for client: ");
  sleep(1);
  kill(cli, SIGUSR1);
  if (waitpid(cli, &status, 0) == -1) return 2;
  if (!WIFEXITED(status)) return 4;
  if (WEXITSTATUS(status)) return WEXITSTATUS(status);
  return 0;
}

int main(int argc, char** argv) {
  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  char root[1024];
  int port = grpc_pick_unused_port_or_die();
  int status;
  pid_t svr;
  int ret;
  /* figure out where we are */
  if (lslash) {
    memcpy(root, me, lslash - me);
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }
  /* start the server */
  svr = fork();
  if (svr == 0) {
    char* binary_path;
    char* port_arg;
    gpr_asprintf(&binary_path, "%s/../../examples/cpp/helloworld/greeter_server", root);
    gpr_asprintf(&port_arg, "%d", port);

    execl(binary_path, binary_path, port_arg, NULL);

    gpr_free(binary_path);
    gpr_free(port_arg);
    return 1;
  }
  /* wait a little */
  sleep(2);
  /* start the clients */
  ret = test_client(root, port);
  gpr_log(GPR_INFO, "test_client returned %d", ret);
  /* wait for server */
  gpr_log(GPR_INFO, "Waiting for server");
  kill(svr, SIGINT);
  if (waitpid(svr, &status, 0) == -1) return 2;
  if (!WIFEXITED(status)) return 4;
  if (WEXITSTATUS(status)) return WEXITSTATUS(status);
  return 0;
}
