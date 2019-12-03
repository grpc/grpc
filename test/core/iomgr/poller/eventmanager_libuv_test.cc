
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

#include "src/core/lib/iomgr/poller/eventmanager_libuv.h"

#include <grpc/grpc.h>
#include <grpc/support/time.h>

#include "test/core/util/test_config.h"

using grpc::experimental::LibuvEventManager;

static void test_manager_allocation() {
  for (int i = 0; i < 10; i++) {
    LibuvEventManager* em = new LibuvEventManager(i);
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1));
    delete em;
  }
}

static void test_shutdown_ref() {
  for (int i = 0; i < 10; i++) {
    LibuvEventManager* em = new LibuvEventManager(i);
    for (int j = 0; j < i; j++) {
      em->ShutdownRef();
    }
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1));
    for (int j = 0; j < i; j++) {
      em->ShutdownUnref();
    }
    delete em;
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_manager_allocation();
  test_shutdown_ref();
  grpc_shutdown();
  return 0;
}
