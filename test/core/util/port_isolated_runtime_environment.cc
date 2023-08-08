//
//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#if defined(GRPC_PORT_ISOLATED_RUNTIME)

// When individual tests run in an isolated runtime environment (e.g. each test
// runs in a separate container) the framework takes a round-robin pick of a
// port within certain range. There is no need to recycle ports.
//
#include <stdlib.h>

#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define MIN_PORT 1025
#define MAX_PORT 4096

static int get_random_port_offset() {
  srand(gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  double rnd = static_cast<double>(rand()) /
               (static_cast<double>(RAND_MAX) + 1.0);  // values from [0,1)
  return static_cast<int>(rnd * (MAX_PORT - MIN_PORT + 1));
}

static int s_initial_offset = get_random_port_offset();
static gpr_atm s_pick_counter = 0;

static int grpc_pick_unused_port_or_die_impl(void) {
  int orig_counter_val =
      static_cast<int>(gpr_atm_full_fetch_add(&s_pick_counter, 1));
  GPR_ASSERT(orig_counter_val < (MAX_PORT - MIN_PORT + 1));
  return MIN_PORT +
         (s_initial_offset + orig_counter_val) % (MAX_PORT - MIN_PORT + 1);
}

static int isolated_pick_unused_port_or_die(void) {
  while (true) {
    int port = grpc_pick_unused_port_or_die_impl();
    // 5985 cannot be bound on Windows RBE and results in
    // WSA_ERROR 10013: "An attempt was made to access a socket in a way
    // forbidden by its access permissions."
    if (port == 5985) {
      continue;
    }
    return port;
  }
}

static void isolated_recycle_unused_port(int port) { (void)port; }

// We don't actually use prev_fns for anything, but need to save it in order to
// be able to call grpc_set_pick_port_functions() to override defaults for this
// environment.
static const auto prev_fns =
    grpc_set_pick_port_functions(grpc_pick_port_functions{
        isolated_pick_unused_port_or_die, isolated_recycle_unused_port});

#endif  // GRPC_PORT_ISOLATED_RUNTIME
