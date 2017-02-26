/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/support/string.h"
#include "test/core/util/port.h"

int main(int argc, char **argv) {
  char *me = argv[0];
  char *lslash = strrchr(me, '/');
  char root[1024];
  int port = grpc_pick_unused_port_or_die();
  char *args[10];
  int status;
  pid_t svr, cli;
  /* seed rng with pid, so we don't end up with the same random numbers as a
     concurrently running test binary */
  srand((unsigned)getpid());
  /* figure out where we are */
  if (lslash) {
    memcpy(root, me, (size_t)(lslash - me));
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }
  /* start the server */
  svr = fork();
  if (svr == 0) {
    gpr_asprintf(&args[0], "%s/fling_server", root);
    args[1] = "--bind";
    gpr_join_host_port(&args[2], "::", port);
    args[3] = "--no-secure";
    args[4] = 0;
    execv(args[0], args);

    gpr_free(args[0]);
    gpr_free(args[2]);
    return 1;
  }
  /* wait a little */
  sleep(2);
  /* start the client */
  cli = fork();
  if (cli == 0) {
    gpr_asprintf(&args[0], "%s/fling_client", root);
    args[1] = "--target";
    gpr_join_host_port(&args[2], "127.0.0.1", port);
    args[3] = "--scenario=ping-pong-stream";
    args[4] = "--no-secure";
    args[5] = 0;
    execv(args[0], args);

    gpr_free(args[0]);
    gpr_free(args[2]);
    return 1;
  }
  /* wait for completion */
  printf("waiting for client\n");
  if (waitpid(cli, &status, 0) == -1) return 2;
  if (!WIFEXITED(status)) return 4;
  if (WEXITSTATUS(status)) return WEXITSTATUS(status);
  printf("waiting for server\n");
  kill(svr, SIGINT);
  if (waitpid(svr, &status, 0) == -1) return 2;
  if (!WIFEXITED(status)) return 4;
  if (WEXITSTATUS(status)) return WEXITSTATUS(status);
  return 0;
}
