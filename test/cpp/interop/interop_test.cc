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

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gflags/gflags.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/util/port.h"
#include "test/cpp/util/test_config.h"

extern "C" {
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/support/string.h"
}

DEFINE_string(extra_server_flags, "", "Extra flags to pass to server.");

int test_client(const char* root, const char* host, int port) {
  int status;
  pid_t cli;
  cli = fork();
  if (cli == 0) {
    char* binary_path;
    char* port_arg;
    gpr_asprintf(&binary_path, "%s/interop_client", root);
    gpr_asprintf(&port_arg, "--server_port=%d", port);

    execl(binary_path, binary_path, port_arg, NULL);

    gpr_free(binary_path);
    gpr_free(port_arg);
    return 1;
  }
  /* wait for client */
  gpr_log(GPR_INFO, "Waiting for client: %s", host);
  if (waitpid(cli, &status, 0) == -1) return 2;
  if (!WIFEXITED(status)) return 4;
  if (WEXITSTATUS(status)) return WEXITSTATUS(status);
  return 0;
}

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  char root[1024];
  int port = grpc_pick_unused_port_or_die();
  int status;
  pid_t svr;
  int ret;
  int do_ipv6 = 1;
  /* seed rng with pid, so we don't end up with the same random numbers as a
     concurrently running test binary */
  srand(getpid());
  if (!grpc_ipv6_loopback_available()) {
    gpr_log(GPR_INFO, "Can't bind to ::1.  Skipping IPv6 tests.");
    do_ipv6 = 0;
  }
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
    const size_t num_args = 3 + !FLAGS_extra_server_flags.empty();
    char** args = (char**)gpr_malloc(sizeof(char*) * num_args);
    memset(args, 0, sizeof(char*) * num_args);
    gpr_asprintf(&args[0], "%s/interop_server", root);
    gpr_asprintf(&args[1], "--port=%d", port);
    if (!FLAGS_extra_server_flags.empty()) {
      args[2] = gpr_strdup(FLAGS_extra_server_flags.c_str());
    }
    execv(args[0], args);
    for (size_t i = 0; i < num_args - 1; ++i) {
      gpr_free(args[i]);
    }
    gpr_free(args);
    return 1;
  }
  /* wait a little */
  sleep(10);
  /* start the clients */
  ret = test_client(root, "127.0.0.1", port);
  if (ret != 0) return ret;
  ret = test_client(root, "::ffff:127.0.0.1", port);
  if (ret != 0) return ret;
  ret = test_client(root, "localhost", port);
  if (ret != 0) return ret;
  if (do_ipv6) {
    ret = test_client(root, "::1", port);
    if (ret != 0) return ret;
  }
  /* wait for server */
  gpr_log(GPR_INFO, "Waiting for server");
  kill(svr, SIGINT);
  if (waitpid(svr, &status, 0) == -1) return 2;
  if (!WIFEXITED(status)) return 4;
  if (WEXITSTATUS(status)) return WEXITSTATUS(status);
  return 0;
}
