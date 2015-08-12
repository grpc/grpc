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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <grpc/support/socket_util.h>
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/subprocess.h>
#include "test/core/util/port.h"
#include <grpc/support/time.h>

int test_client(const char* root, const char* host, int port) {
  int status;
  gpr_subprocess *cli;
  char *args[10];

  gpr_asprintf(&args[0], "%s/interop_client%s", root, gpr_subprocess_binary_extension());
  gpr_asprintf(&args[1], "--server_port=%d", port);

  cli = gpr_subprocess_create(2, (const char**)args);
  gpr_free(args[0]);
  gpr_free(args[1]);

  gpr_log(GPR_INFO, "Waiting for client: %s", host);
  status = gpr_subprocess_join(cli);
  gpr_subprocess_destroy(cli);
  return status;

}


int main(int argc, char** argv) {
  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  char root[1024];
  int port = grpc_pick_unused_port_or_die();
  int status;
  gpr_subprocess *svr;
  int ret;
  int do_ipv6 = 1;
  char *args[10];

  /* seed rng with pid, so we don't end up with the same random numbers as a
     concurrently running test binary */
//this is done in /test/core/util/test_config.c
//  srand(getpid());
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
  gpr_asprintf(&args[0], "%s/interop_server%s", root, gpr_subprocess_binary_extension());
  gpr_asprintf(&args[1], "--port=%d", port);
  svr = gpr_subprocess_create(2, (const char**)args);
  gpr_free(args[0]);
  gpr_free(args[1]);

  /* wait a little */
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                   gpr_time_from_seconds(2, GPR_TIMESPAN)));

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
  gpr_subprocess_interrupt(svr);
  status = gpr_subprocess_join(svr);
  gpr_subprocess_destroy(svr);
  return status;
}
