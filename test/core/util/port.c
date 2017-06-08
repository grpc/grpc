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
#include "test/core/util/test_config.h"
#if defined(GRPC_TEST_PICK_PORT)

#include "test/core/util/port.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "test/core/util/port_server_client.h"

static int *chosen_ports = NULL;
static size_t num_chosen_ports = 0;

static int free_chosen_port(int port) {
  size_t i;
  int found = 0;
  size_t found_at = 0;
  /* Find the port and erase it from the list, then tell the server it can be
     freed. */
  for (i = 0; i < num_chosen_ports; i++) {
    if (chosen_ports[i] == port) {
      GPR_ASSERT(found == 0);
      found = 1;
      found_at = i;
    }
  }
  if (found) {
    chosen_ports[found_at] = chosen_ports[num_chosen_ports - 1];
    num_chosen_ports--;
    grpc_free_port_using_server(port);
  }
  return found;
}

static void free_chosen_ports(void) {
  size_t i;
  grpc_init();
  for (i = 0; i < num_chosen_ports; i++) {
    grpc_free_port_using_server(chosen_ports[i]);
  }
  grpc_shutdown();
  gpr_free(chosen_ports);
}

static void chose_port(int port) {
  if (chosen_ports == NULL) {
    atexit(free_chosen_ports);
  }
  num_chosen_ports++;
  chosen_ports = gpr_realloc(chosen_ports, sizeof(int) * num_chosen_ports);
  chosen_ports[num_chosen_ports - 1] = port;
}

int grpc_pick_unused_port(void) {
  int port = grpc_pick_port_using_server();
  if (port != 0) {
    chose_port(port);
  }

  return port;
}

int grpc_pick_unused_port_or_die(void) {
  int port = grpc_pick_unused_port();
  if (port == 0) {
    fprintf(stderr,
            "gRPC tests require a helper port server to allocate ports used \n"
            "during the test.\n\n"
            "This server is not currently running.\n\n"
            "To start it, run tools/run_tests/start_port_server.py\n\n");
    exit(1);
  }
  return port;
}

void grpc_recycle_unused_port(int port) { GPR_ASSERT(free_chosen_port(port)); }

#endif /* GRPC_TEST_PICK_PORT */
