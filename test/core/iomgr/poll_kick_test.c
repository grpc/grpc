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

#include "src/core/iomgr/pollset_kick.h"

#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static void test_allocation() {
  grpc_pollset_kick_state state;
  grpc_pollset_kick_init(&state);
  grpc_pollset_kick_destroy(&state);
}

static void test_non_kick() {
  grpc_pollset_kick_state state;
  int fd;

  grpc_pollset_kick_init(&state);
  fd = grpc_pollset_kick_pre_poll(&state);
  GPR_ASSERT(fd >= 0);

  grpc_pollset_kick_post_poll(&state);
  grpc_pollset_kick_destroy(&state);
}

static void test_basic_kick() {
  /* Kicked during poll */
  grpc_pollset_kick_state state;
  int fd;
  grpc_pollset_kick_init(&state);

  fd = grpc_pollset_kick_pre_poll(&state);
  GPR_ASSERT(fd >= 0);

  grpc_pollset_kick_kick(&state);

  /* Now hypothetically we polled and found that we were kicked */
  grpc_pollset_kick_consume(&state);

  grpc_pollset_kick_post_poll(&state);

  grpc_pollset_kick_destroy(&state);
}

static void test_non_poll_kick() {
  /* Kick before entering poll */
  grpc_pollset_kick_state state;
  int fd;

  grpc_pollset_kick_init(&state);

  grpc_pollset_kick_kick(&state);
  fd = grpc_pollset_kick_pre_poll(&state);
  GPR_ASSERT(fd < 0);
  grpc_pollset_kick_destroy(&state);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  grpc_pollset_kick_global_init();

  test_allocation();
  test_basic_kick();
  test_non_poll_kick();
  test_non_kick();

  grpc_pollset_kick_global_destroy();
  return 0;
}
