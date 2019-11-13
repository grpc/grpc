/*
 *
 * Copyright 2019 gRPC authors.
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
#if defined(GRPC_WINSOCK_SOCKET)

#include <vector>

#include <grpc/grpc.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_windows.h"
#include "src/core/lib/surface/init.h"
#include "test/core/util/test_config.h"

struct ThreadParams {
  gpr_cv cv;
  gpr_mu mu;
  int complete;
};

int main(int argc, char** argv) {
  grpc_init();

  // Create three threads that all start queueing for work.
  //
  // The first one becomes the active poller for work and the two other
  // threads go into the poller queue.
  //
  // When work arrives, the first one notifies the next active poller,
  // this wakes the second thread - however all this does is return from
  // the grpc_pollset_work function. It's up to that thread to figure
  // out if it still wants to queue for more work or if it should kick
  // other pollers.
  //
  // Previously that kick only affected pollers in the same pollset, thus
  // leaving the third thread stuck in the poller queue. Now the pollset-
  // specific grpc_pollset_kick will also kick pollers from other pollsets
  // if there are no pollers in the current pollset. This frees up the
  // last thread and completes the test.
  ThreadParams params = {};
  gpr_cv_init(&params.cv);
  gpr_mu_init(&params.mu);
  std::vector<grpc_core::Thread> threads;
  for (int i = 0; i < 3; i++) {
    grpc_core::Thread thd(
        "Poller",
        [](void* params) {
          ThreadParams* tparams = static_cast<ThreadParams*>(params);
          grpc_core::ExecCtx exec_ctx;

          gpr_mu* mu;
          grpc_pollset pollset = {};
          grpc_pollset_init(&pollset, &mu);

          gpr_mu_lock(mu);

          // Queue for work and once we're done, make sure to kick the remaining
          // threads.
          grpc_millis deadline = grpc_timespec_to_millis_round_up(
              grpc_timeout_seconds_to_deadline(5));
          grpc_error* error;
          error = grpc_pollset_work(&pollset, NULL, deadline);
          error = grpc_pollset_kick(&pollset, NULL);

          gpr_mu_unlock(mu);

          {
            gpr_mu_lock(&tparams->mu);
            tparams->complete++;
            gpr_cv_signal(&tparams->cv);
            gpr_mu_unlock(&tparams->mu);
          }
        },
        &params);
    thd.Start();
    threads.push_back(std::move(thd));
  }

  // Wait for the threads to start working and then kick one of them.
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(10));
  grpc_iocp_kick();

  // Wait for the threads to complete.
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(1);
  gpr_mu_lock(&params.mu);
  while (params.complete != 3 && !gpr_cv_wait(&params.cv, &params.mu, deadline))
    ;
  if (params.complete != 3) {
    gpr_mu_unlock(&params.mu);
    for (auto& t : threads) t.Join();
    return EXIT_FAILURE;
  }

  gpr_mu_unlock(&params.mu);
  for (auto& t : threads) t.Join();
  return EXIT_SUCCESS;
}
#else /* defined(GRPC_WINSOCK_SOCKET) */
int main(int /*argc*/, char** /*argv*/) { return 0; }
#endif
