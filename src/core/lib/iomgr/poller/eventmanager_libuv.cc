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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/poller/eventmanager_libuv.h"

#include <grpc/support/time.h>

grpc::experimental::LibuvEventManager::Options::Options() : num_workers_(-1) {}
grpc::experimental::LibuvEventManager::Options::Options(int num_workers)
    : num_workers_(num_workers) {}

grpc::experimental::LibuvEventManager::LibuvEventManager(const Options& options)
    : options_(options) {
  int num_workers = options_.num_workers();
  // Number of workers can't be 0 if we do not accept thread donation.
  // TODO(guantaol): replaces the hard-coded number with a flag.
  if (num_workers <= 0) num_workers = 32;

  for (int i = 0; i < num_workers; i++) {
    workers_.emplace_back(
        options_.thread_name_prefix().c_str(),
        [](void* em) { static_cast<LibuvEventManager*>(em)->RunWorkerLoop(); },
        this);
    workers_.back().Start();
  }
}

grpc::experimental::LibuvEventManager::~LibuvEventManager() {
  Shutdown();
  for (auto& th : workers_) {
    th.Join();
  }
}

void grpc::experimental::LibuvEventManager::RunWorkerLoop() {
  while (true) {
    // TODO(guantaol): extend the worker loop with real work.
    if (ShouldStop()) return;
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                 gpr_time_from_micros(10, GPR_TIMESPAN)));
  }
}

bool grpc::experimental::LibuvEventManager::ShouldStop() {
  return should_stop_.Load(grpc_core::MemoryOrder::ACQUIRE) != 0;
}

void grpc::experimental::LibuvEventManager::Shutdown() {
  if (should_stop_.Load(grpc_core::MemoryOrder::ACQUIRE))
    return;  // Already shut down.

  {
    grpc_core::MutexLock lock(&shutdown_mu_);
    while (shutdown_refcount_.Load(grpc_core::MemoryOrder::ACQUIRE) > 0) {
      shutdown_cv_.Wait(&shutdown_mu_);
    }
  }
  should_stop_.Store(true, grpc_core::MemoryOrder::RELEASE);
}

void grpc::experimental::LibuvEventManager::ShutdownRef() {
  shutdown_refcount_.FetchAdd(1, grpc_core::MemoryOrder::RELAXED);
}

void grpc::experimental::LibuvEventManager::ShutdownUnref() {
  if (shutdown_refcount_.FetchSub(1, grpc_core::MemoryOrder::ACQ_REL) == 1) {
    grpc_core::MutexLock lock(&shutdown_mu_);
    shutdown_cv_.Signal();
  }
}
