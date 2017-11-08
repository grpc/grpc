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

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static int g_flag;

static void test(int rounds) {
  int i;
  for (i = 0; i < rounds; i++) {
    grpc_init();
  }
  for (i = 0; i < rounds; i++) {
    grpc_shutdown();
  }
}

static void test_mixed(void) {
  grpc_init();
  grpc_init();
  grpc_shutdown();
  grpc_init();
  grpc_shutdown();
  grpc_shutdown();
}

static void plugin_init(void) { g_flag = 1; }
static void plugin_destroy(void) { g_flag = 2; }

static void test_plugin() {
  grpc_register_plugin(plugin_init, plugin_destroy);
  grpc_init();
  GPR_ASSERT(g_flag == 1);
  grpc_shutdown();
  GPR_ASSERT(g_flag == 2);
}

static void test_repeatedly() {
  for (int i = 0; i < 1000; i++) {
    grpc_init();
    grpc_shutdown();
  }
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  test(1);
  test(2);
  test(3);
  test_mixed();
  test_plugin();
  test_repeatedly();
  return 0;
}
