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

#include "src/core/lib/iomgr/tcp_server.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <vector>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_windows.h"
#include "src/core/lib/surface/init.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", #x)

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
  std::condition_variable cv;
  std::mutex m;
  int complete = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < 3; i++) {
    threads.push_back(std::thread([&]() {
      grpc_core::ExecCtx exec_ctx;

      gpr_mu* g_mu;
      grpc_pollset g_pollset = {};
      grpc_pollset_init(&g_pollset, &g_mu);

      gpr_mu_lock(g_mu);

      // Queue for work and once we're done, make sure to kick the remaining
      // threads.
      grpc_error* error;
      error = grpc_pollset_work(&g_pollset, NULL, GRPC_MILLIS_INF_FUTURE);
      error = grpc_pollset_kick(&g_pollset, NULL);

      gpr_mu_unlock(g_mu);

      {
        std::unique_lock<std::mutex> lock(m);
        complete++;
        cv.notify_all();
      }
    }));
  }

  // Wait for the threads to start working and then kick one of them.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  grpc_iocp_kick();

  // Wait for the threads to complete.
  {
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, std::chrono::seconds(1),
                     [&] { return complete == 3; }))
      return EXIT_FAILURE;
  }

  for (auto& t : threads) t.join();

  return EXIT_SUCCESS;
}
