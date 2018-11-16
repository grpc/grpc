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

/* When individual tests run in an isolated runtime environment (e.g. each test
 * runs in a separate container) the framework takes a round-robin pick of a
 * port within certain range. There is no need to recycle ports.
 */
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stdlib.h>
#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"
#if defined(GRPC_PORT_ISOLATED_RUNTIME)

#include "test/core/util/port.h"

#define MIN_PORT 1025
#define MAX_PORT 32766

static int get_random_port_offset() {
  srand(gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  double rnd = static_cast<double>(rand()) /
               (static_cast<double>(RAND_MAX) + 1.0);  // values from [0,1)
  return static_cast<int>(rnd * (MAX_PORT - MIN_PORT + 1));
}

static int s_initial_offset = get_random_port_offset();
static gpr_atm s_pick_counter = 0;

int grpc_pick_unused_port_or_die(void) {
  int orig_counter_val =
      static_cast<int>(gpr_atm_full_fetch_add(&s_pick_counter, 1));
  GPR_ASSERT(orig_counter_val < (MAX_PORT - MIN_PORT + 1));
  return MIN_PORT +
         (s_initial_offset + orig_counter_val) % (MAX_PORT - MIN_PORT + 1);
}

void grpc_recycle_unused_port(int port) { (void)port; }

#endif /* GRPC_PORT_ISOLATED_RUNTIME */
