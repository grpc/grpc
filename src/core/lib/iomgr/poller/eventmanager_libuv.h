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

#ifndef GRPC_CORE_LIB_IOMGR_POLLER_EVENTMANAGER_LIBUV_H
#define GRPC_CORE_LIB_IOMGR_POLLER_EVENTMANAGER_LIBUV_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"

namespace grpc {
namespace experimental {

class LibuvEventManager {
 public:
  class Options {
   public:
    Options();
    Options(int num_workers);

    int num_workers() const { return num_workers_; }
    void set_num_workers(int num) { num_workers_ = num; }

    const std::string& thread_name_prefix() const {
      return thread_name_prefix_;
    }
    void set_thread_name_prefix(const std::string& name) {
      thread_name_prefix_ = name;
    }

   private:
    // Number of worker threads to create at startup. If less than 0, uses the
    // default value of 32.
    int num_workers_;
    // Name prefix used for worker.
    std::string thread_name_prefix_;
  };

  explicit LibuvEventManager(const Options& options);
  virtual ~LibuvEventManager();

  void Shutdown();
  void ShutdownRef();
  void ShutdownUnref();

 private:
  // Function run by the worker threads.
  void RunWorkerLoop();

  // Whether the EventManager has been shut down.
  bool ShouldStop();

  const Options options_;
  // Whether the EventManager workers should be stopped.
  grpc_core::Atomic<bool> should_stop_{false};
  // A refcount preventing the EventManager from shutdown.
  grpc_core::Atomic<int> shutdown_refcount_{0};
  // Worker threads of the EventManager.
  std::vector<grpc_core::Thread> workers_;
  // Mutex and condition variable used for shutdown.
  grpc_core::Mutex shutdown_mu_;
  grpc_core::CondVar shutdown_cv_;
};

}  // namespace experimental
}  // namespace grpc

#endif /* GRPC_CORE_LIB_IOMGR_POLLER_EVENTMANAGER_LIBUV_H */
