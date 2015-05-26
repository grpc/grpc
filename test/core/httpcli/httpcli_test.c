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

#include "src/core/httpcli/httpcli.h"

#include <string.h>

#include "src/core/iomgr/iomgr.h"
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include "test/core/util/test_config.h"

static gpr_event g_done;

static gpr_timespec n_seconds_time(int seconds) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(seconds);
}

static void on_finish(void *arg, const grpc_httpcli_response *response) {
  GPR_ASSERT(arg == (void *)42);
  GPR_ASSERT(response);
  GPR_ASSERT(response->status == 200);
  gpr_event_set(&g_done, (void *)1);
}

static void test_get(int use_ssl) {
  grpc_httpcli_request req;

  gpr_log(GPR_INFO, "running %s with use_ssl=%d.", "test_get", use_ssl);

  gpr_event_init(&g_done);
  memset(&req, 0, sizeof(req));
  req.host = "www.google.com";
  req.path = "/";
  req.use_ssl = use_ssl;

  grpc_httpcli_get(&req, n_seconds_time(15), on_finish, (void *)42);
  GPR_ASSERT(gpr_event_wait(&g_done, n_seconds_time(20)));
}

/*
static void test_post(int use_ssl) {
  grpc_httpcli_request req;

  gpr_log(GPR_INFO, "running %s with use_ssl=%d.", "test_post", (int)use_ssl);

  gpr_event_init(&g_done);
  memset(&req, 0, sizeof(req));
  req.host = "requestb.in";
  req.path = "/1eamwr21";
  req.use_ssl = use_ssl;

  grpc_httpcli_post(&req, NULL, 0, n_seconds_time(15), on_finish,
                    (void *)42);
  GPR_ASSERT(gpr_event_wait(&g_done, n_seconds_time(20)));
}
*/

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_iomgr_init();

  test_get(0);
  test_get(1);

  /* test_post(0); */
  /* test_post(1); */

  grpc_iomgr_shutdown();

  return 0;
}
