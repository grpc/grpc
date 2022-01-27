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

#if defined(GRPC_WINSOCK_SOCKET)

// At least three threads are required to reproduce #18848
const size_t THREADS = 3;

struct ThreadParams {
  gpr_cv cv;
  gpr_mu mu;
  int complete;
  int queuing;
  gpr_mu* pollset_mu[THREADS];
};

int main(int argc, char** argv) {
  grpc_init();

  // Create the threads that all start queueing for work.
  //
  // The first one becomes the active poller for work and the two other
  // threads go into the poller queue.
  //
  // When work arrives, the first one notifies the next queued poller,
  // this wakes the second thread - however all this does is return from
  // the grpc_pollset_work function. It's up to that thread to figure
  // out if it still wants to queue for more work or if it should kick
  // other pollers.
  //
  // Previously that kick only affected pollers in the same pollset, thus
  // leaving the other threads stuck in the poller queue. Now the pollset-
  // specific grpc_pollset_kick will also kick pollers from other pollsets
  // if there are no pollers in the current pollset. This frees up the
  // last threads and completes the test.
  ThreadParams params = {};
  gpr_cv_init(&params.cv);
  gpr_mu_init(&params.mu);
  std::vector<grpc_core::Thread> threads;
  for (int i = 0; i < THREADS; i++) {
    grpc_core::Thread thd(
        "Poller",
        [](void* params) {
          ThreadParams* tparams = static_cast<ThreadParams*>(params);
          grpc_core::ExecCtx exec_ctx;

          gpr_mu* mu;
          grpc_pollset pollset = {};
          grpc_pollset_init(&pollset, &mu);

          // Lock the pollset mutex before notifying the test runner thread that
          // one more thread is queuing. This allows the test runner thread to
          // wait for all threads to be queued before sending the first kick by
          // waiting for the mutexes to be released, which happens in
          // gpr_pollset_work when the poller is queued.
          gpr_mu_lock(mu);

          gpr_mu_lock(&tparams->mu);
          tparams->pollset_mu[tparams->queuing] = mu;
          tparams->queuing++;
          gpr_cv_signal(&tparams->cv);
          gpr_mu_unlock(&tparams->mu);

          // Queue for work and once we're done, make sure to kick the remaining
          // threads.
          grpc_error_handle error;
          error = grpc_pollset_work(&pollset, NULL, GRPC_MILLIS_INF_FUTURE);
          error = grpc_pollset_kick(&pollset, NULL);

          gpr_mu_unlock(mu);

          gpr_mu_lock(&tparams->mu);
          tparams->complete++;
          gpr_cv_signal(&tparams->cv);
          gpr_mu_unlock(&tparams->mu);
        },
        &params);
    thd.Start();
    threads.push_back(std::move(thd));
  }

  // Wait for all three threads to be queuing.
  gpr_mu_lock(&params.mu);
  while (
      params.queuing != THREADS &&
      !gpr_cv_wait(&params.cv, &params.mu, gpr_inf_future(GPR_CLOCK_REALTIME)))
    ;
  gpr_mu_unlock(&params.mu);

  // Wait for the mutexes to be released. This indicates that the threads have
  // entered the work wait.
  //
  // At least currently these are essentially all references to the same global
  // pollset mutex, but we are still waiting on them once for each thread in
  // the case this ever changes.
  for (int i = 0; i < THREADS; i++) {
    gpr_mu_lock(params.pollset_mu[i]);
    gpr_mu_unlock(params.pollset_mu[i]);
  }

  grpc_iocp_kick();

  // Wait for the threads to complete.
  gpr_mu_lock(&params.mu);
  while (
      params.complete != THREADS &&
      !gpr_cv_wait(&params.cv, &params.mu, gpr_inf_future(GPR_CLOCK_REALTIME)))
    ;
  gpr_mu_unlock(&params.mu);

  for (auto& t : threads) t.Join();
  return EXIT_SUCCESS;
}
#else /* defined(GRPC_WINSOCK_SOCKET) */
int main(int /*argc*/, char** /*argv*/) { return 0; }
#endif
