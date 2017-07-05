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

#include <stdio.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/security/credentials/composite/composite_credentials.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"

typedef struct {
  gpr_mu *mu;
  grpc_polling_entity pops;
  int is_done;
} synchronizer;

static void on_metadata_response(grpc_exec_ctx *exec_ctx, void *user_data,
                                 grpc_credentials_md *md_elems, size_t num_md,
                                 grpc_credentials_status status,
                                 const char *error_details) {
  synchronizer *sync = user_data;
  if (status == GRPC_CREDENTIALS_ERROR) {
    fprintf(stderr, "Fetching token failed.\n");
  } else {
    char *token;
    GPR_ASSERT(num_md == 1);
    token = grpc_slice_to_c_string(md_elems[0].value);
    printf("\nGot token: %s\n\n", token);
    gpr_free(token);
  }
  gpr_mu_lock(sync->mu);
  sync->is_done = 1;
  GRPC_LOG_IF_ERROR(
      "pollset_kick",
      grpc_pollset_kick(grpc_polling_entity_pollset(&sync->pops), NULL));
  gpr_mu_unlock(sync->mu);
}

int main(int argc, char **argv) {
  int result = 0;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  synchronizer sync;
  grpc_channel_credentials *creds = NULL;
  char *service_url = "https://test.foo.google.com/Foo";
  grpc_auth_metadata_context context;
  gpr_cmdline *cl = gpr_cmdline_create("print_google_default_creds_token");
  gpr_cmdline_add_string(cl, "service_url",
                         "Service URL for the token request.", &service_url);
  gpr_cmdline_parse(cl, argc, argv);
  memset(&context, 0, sizeof(context));
  context.service_url = service_url;

  grpc_init();

  creds = grpc_google_default_credentials_create();
  if (creds == NULL) {
    fprintf(stderr, "\nCould not find default credentials.\n\n");
    result = 1;
    goto end;
  }

  grpc_pollset *pollset = gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(pollset, &sync.mu);
  sync.pops = grpc_polling_entity_create_from_pollset(pollset);
  sync.is_done = 0;

  grpc_call_credentials_get_request_metadata(
      &exec_ctx, ((grpc_composite_channel_credentials *)creds)->call_creds,
      &sync.pops, context, on_metadata_response, &sync);

  gpr_mu_lock(sync.mu);
  while (!sync.is_done) {
    grpc_pollset_worker *worker = NULL;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(&exec_ctx,
                              grpc_polling_entity_pollset(&sync.pops), &worker,
                              gpr_now(GPR_CLOCK_MONOTONIC),
                              gpr_inf_future(GPR_CLOCK_MONOTONIC))))
      sync.is_done = 1;
    gpr_mu_unlock(sync.mu);
    grpc_exec_ctx_flush(&exec_ctx);
    gpr_mu_lock(sync.mu);
  }
  gpr_mu_unlock(sync.mu);

  grpc_exec_ctx_finish(&exec_ctx);

  grpc_channel_credentials_release(creds);
  gpr_free(grpc_polling_entity_pollset(&sync.pops));

end:
  gpr_cmdline_destroy(cl);
  grpc_shutdown();
  return result;
}
