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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/subprocess.h>
#include <grpc/support/sync.h>
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static gpr_event g_done;

static gpr_timespec n_seconds_time(int seconds) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(seconds);
}

static void on_finish(void *arg, const grpc_httpcli_response *response) {
  const char *expect = 
      "<html><head><title>Hello world!</title></head>"
      "<body><p>This is a test</p></body></html>";
  GPR_ASSERT(arg == (void *)42);
  GPR_ASSERT(response);
  GPR_ASSERT(response->status == 200);
  GPR_ASSERT(response->body_length == strlen(expect));
  GPR_ASSERT(0 == memcmp(expect, response->body, response->body_length));
  gpr_event_set(&g_done, (void *)1);
}

static void test_get(int use_ssl, int port) {
  grpc_httpcli_request req;
  char* host;

  gpr_log(GPR_INFO, "running %s with use_ssl=%d.", "test_get", use_ssl);

  gpr_asprintf(&host, "localhost:%d", port);
  gpr_log(GPR_INFO, "requesting from %s", host);

  gpr_event_init(&g_done);
  memset(&req, 0, sizeof(req));
  req.host = host;
  req.path = "/get";
  req.use_ssl = use_ssl;

  grpc_httpcli_get(&req, n_seconds_time(15), on_finish, (void *)42);
  gpr_free(host);
  GPR_ASSERT(gpr_event_wait(&g_done, n_seconds_time(20)));
}

static void test_post(int use_ssl, int port) {
  grpc_httpcli_request req;
  char* host;

  gpr_log(GPR_INFO, "running %s with use_ssl=%d.", "test_post", (int)use_ssl);

  gpr_asprintf(&host, "localhost:%d", port);
  gpr_log(GPR_INFO, "posting to %s", host);

  gpr_event_init(&g_done);
  memset(&req, 0, sizeof(req));
  req.host = host;
  req.path = "/post";
  req.use_ssl = use_ssl;

  grpc_httpcli_post(&req, "hello", 5, n_seconds_time(15), on_finish,
                    (void *)42);
  GPR_ASSERT(gpr_event_wait(&g_done, n_seconds_time(20)));
}

int main(int argc, char **argv) {
  gpr_subprocess* server;
  char *me = argv[0];
  char *lslash = strrchr(me, '/');
  char* args[4];
  char root[1024];
  int port = grpc_pick_unused_port_or_die();

  /* figure out where we are */
  if (lslash) {
    memcpy(root, me, lslash - me);
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }

  /* start the server */
  gpr_asprintf(&args[0], "%s/../../test/core/httpcli/test_server.py", root);
  args[1] = "--port";
  gpr_asprintf(&args[2], "%d", port);
  server = gpr_subprocess_create(3, (const char**)args);
  GPR_ASSERT(server);
  gpr_free(args[0]);
  gpr_free(args[2]);

  gpr_sleep_until(gpr_time_add(gpr_now(), gpr_time_from_seconds(5)));

  grpc_test_init(argc, argv);
  grpc_iomgr_init();

  test_get(0, port);
  test_post(0, port);

  grpc_iomgr_shutdown();

  gpr_subprocess_destroy(server);

  return 0;
}
