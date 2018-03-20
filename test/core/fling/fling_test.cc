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

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"

int main(int argc, const char** argv) {
  const char* me = argv[0];
  const char* lslash = strrchr(me, '/');
  char root[1024];
  int port = grpc_pick_unused_port_or_die();
  char* args[10];
  int status;
  gpr_subprocess *svr, *cli;
  /* figure out where we are */
  if (lslash) {
    memcpy(root, me, static_cast<size_t>(lslash - me));
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }
  /* start the server */
  gpr_asprintf(&args[0], "%s/fling_server%s", root,
               gpr_subprocess_binary_extension());
  args[1] = const_cast<char*>("--bind");
  gpr_join_host_port(&args[2], "::", port);
  args[3] = const_cast<char*>("--no-secure");
  svr = gpr_subprocess_create(4, (const char**)args);
  gpr_free(args[0]);
  gpr_free(args[2]);

  /* start the client */
  gpr_asprintf(&args[0], "%s/fling_client%s", root,
               gpr_subprocess_binary_extension());
  args[1] = const_cast<char*>("--target");
  gpr_join_host_port(&args[2], "127.0.0.1", port);
  args[3] = const_cast<char*>("--scenario=ping-pong-request");
  args[4] = const_cast<char*>("--no-secure");
  args[5] = nullptr;
  cli = gpr_subprocess_create(6, (const char**)args);
  gpr_free(args[0]);
  gpr_free(args[2]);

  /* wait for completion */
  printf("waiting for client\n");
  if ((status = gpr_subprocess_join(cli))) {
    gpr_subprocess_destroy(cli);
    gpr_subprocess_destroy(svr);
    return status;
  }
  gpr_subprocess_destroy(cli);

  gpr_subprocess_interrupt(svr);
  status = gpr_subprocess_join(svr);
  gpr_subprocess_destroy(svr);
  return status;
}
