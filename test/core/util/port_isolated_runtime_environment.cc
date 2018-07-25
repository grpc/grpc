/*
 *
 * Copyright 2017 gRPC authors.
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

/* When running tests on remote machines, the framework takes a round-robin pick
 * of a port within certain range. There is no need to recycle ports.
 */
#include <grpc/support/time.h>
#include <stdlib.h>
#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"
#if defined(GRPC_PORT_ISOLATED_RUNTIME)

#include "test/core/util/port.h"

#define MIN_PORT 49152
#define MAX_PORT 65536

int get_random_starting_port() {
  srand(gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  return rand() % (MAX_PORT - MIN_PORT + 1) + MIN_PORT;
}

static int s_allocated_port = get_random_starting_port();

int grpc_pick_unused_port_or_die(void) {
  int allocated_port = s_allocated_port++;
  if (s_allocated_port == MAX_PORT) {
    s_allocated_port = MIN_PORT;
  }

  return allocated_port;
}

void grpc_recycle_unused_port(int port) { (void)port; }

#endif /* GRPC_PORT_ISOLATED_RUNTIME */
