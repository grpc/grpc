/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test(1);
  test(2);
  test(3);
  test_mixed();
  test_plugin();
  return 0;
}
