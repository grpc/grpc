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
#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"
#if defined(GRPC_PORT_ISOLATED_RUNTIME)

#include "test/core/util/port.h"

#define LOWER_PORT 49152
static int s_allocated_port = LOWER_PORT;

int grpc_pick_unused_port_or_die(void) {
  int allocated_port = s_allocated_port++;
  if (s_allocated_port == 65536) {
    s_allocated_port = LOWER_PORT;
  }

  return allocated_port;
}

void grpc_recycle_unused_port(int port) { (void)port; }

#endif /* GRPC_PORT_ISOLATED_RUNTIME */
