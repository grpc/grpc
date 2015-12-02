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

#include "test/core/security/oauth2_utils.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include <grpc/support/sync.h>

#include "src/core/security/credentials.h"

typedef struct {
  grpc_pollset pollset;
  int is_done;
  char *token;
} oauth2_request;

static void on_oauth2_response(grpc_exec_ctx *exec_ctx, void *user_data,
                               grpc_credentials_md *md_elems, size_t num_md,
                               grpc_credentials_status status) {
  oauth2_request *request = user_data;
  char *token = NULL;
  gpr_slice token_slice;
  if (status == GRPC_CREDENTIALS_ERROR) {
    gpr_log(GPR_ERROR, "Fetching token failed.");
  } else {
    GPR_ASSERT(num_md == 1);
    token_slice = md_elems[0].value;
    token = gpr_malloc(GPR_SLICE_LENGTH(token_slice) + 1);
    memcpy(token, GPR_SLICE_START_PTR(token_slice),
           GPR_SLICE_LENGTH(token_slice));
    token[GPR_SLICE_LENGTH(token_slice)] = '\0';
  }
  gpr_mu_lock(GRPC_POLLSET_MU(&request->pollset));
  request->is_done = 1;
  request->token = token;
  grpc_pollset_kick(&request->pollset, NULL);
  gpr_mu_unlock(GRPC_POLLSET_MU(&request->pollset));
}

static void do_nothing(grpc_exec_ctx *exec_ctx, void *unused, int success) {}

char *grpc_test_fetch_oauth2_token_with_credentials(
    grpc_call_credentials *creds) {
  oauth2_request request;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure do_nothing_closure;
  grpc_auth_metadata_context null_ctx = {"", "", NULL, NULL};

  grpc_pollset_init(&request.pollset);
  request.is_done = 0;

  grpc_closure_init(&do_nothing_closure, do_nothing, NULL);

  grpc_call_credentials_get_request_metadata(&exec_ctx, creds, &request.pollset,
                                             null_ctx, on_oauth2_response,
                                             &request);

  grpc_exec_ctx_finish(&exec_ctx);

  gpr_mu_lock(GRPC_POLLSET_MU(&request.pollset));
  while (!request.is_done) {
    grpc_pollset_worker worker;
    grpc_pollset_work(&exec_ctx, &request.pollset, &worker,
                      gpr_now(GPR_CLOCK_MONOTONIC),
                      gpr_inf_future(GPR_CLOCK_MONOTONIC));
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&request.pollset));

  grpc_pollset_shutdown(&exec_ctx, &request.pollset, &do_nothing_closure);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_pollset_destroy(&request.pollset);
  return request.token;
}
