/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include <stdio.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/subprocess.h>
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static void *tag(intptr_t t) { return (void *)t; }

static void run_test(const char *target, size_t nops) {
  grpc_channel_credentials *ssl_creds =
      grpc_ssl_credentials_create(NULL, NULL, NULL);
  grpc_channel *channel;
  grpc_call *c;

  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  char *details = NULL;
  size_t details_capacity = 0;
  grpc_status_code status;
  grpc_call_error error;
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5);
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  cq_verifier *cqv = cq_verifier_create(cq);

  grpc_op ops[6];
  grpc_op *op;

  grpc_arg ssl_name_override = {GRPC_ARG_STRING,
                                GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                                {"foo.test.google.fr"}};
  grpc_channel_args args;

  args.num_args = 1;
  args.args = &ssl_name_override;

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  channel = grpc_secure_channel_create(ssl_creds, target, &args, NULL);
  c = grpc_channel_create_call(channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                               "/foo", "foo.test.google.fr:1234", deadline,
                               NULL);

  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_IGNORE_CONNECTIVITY;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, nops, tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cq_expect_completion(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status != GRPC_STATUS_OK);

  grpc_call_destroy(c);
  gpr_free(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);

  grpc_channel_destroy(channel);
  grpc_completion_queue_destroy(cq);
  cq_verifier_destroy(cqv);
  grpc_channel_credentials_release(ssl_creds);
}

int main(int argc, char **argv) {
  char *me = argv[0];
  char *lslash = strrchr(me, '/');
  char *lunder = strrchr(me, '_');
  char *tmp;
  char root[1024];
  char test[64];
  int port = grpc_pick_unused_port_or_die();
  char *args[10];
  int status;
  size_t i;
  gpr_subprocess *svr;
  /* figure out where we are */
  if (lslash) {
    memcpy(root, me, (size_t)(lslash - me));
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }
  if (argc == 2) {
    gpr_setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH", argv[1]);
  }
  /* figure out our test name */
  tmp = lunder - 1;
  while (*tmp != '_') tmp--;
  tmp++;
  memcpy(test, tmp, (size_t)(lunder - tmp));
  /* start the server */
  gpr_asprintf(&args[0], "%s/bad_ssl_%s_server%s", root, test,
               gpr_subprocess_binary_extension());
  args[1] = "--bind";
  gpr_join_host_port(&args[2], "::", port);
  svr = gpr_subprocess_create(4, (const char **)args);
  gpr_free(args[0]);

  for (i = 3; i <= 4; i++) {
    grpc_init();
    run_test(args[2], i);
    grpc_shutdown();
  }
  gpr_free(args[2]);

  gpr_subprocess_interrupt(svr);
  status = gpr_subprocess_join(svr);
  gpr_subprocess_destroy(svr);
  return status;
}
