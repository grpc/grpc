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
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/credentials/composite/composite_credentials.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "test/core/util/cmdline.h"

typedef struct {
  gpr_mu* mu;
  grpc_polling_entity pops;
  bool is_done;

  grpc_credentials_mdelem_array md_array;
  grpc_closure on_request_metadata;
} synchronizer;

static void on_metadata_response(void* arg, grpc_error* error) {
  synchronizer* sync = static_cast<synchronizer*>(arg);
  if (error != GRPC_ERROR_NONE) {
    fprintf(stderr, "Fetching token failed: %s\n", grpc_error_string(error));
    fflush(stderr);
  } else {
    char* token;
    GPR_ASSERT(sync->md_array.size == 1);
    token = grpc_slice_to_c_string(GRPC_MDVALUE(sync->md_array.md[0]));
    printf("\nGot token: %s\n\n", token);
    gpr_free(token);
  }
  gpr_mu_lock(sync->mu);
  sync->is_done = true;
  GRPC_LOG_IF_ERROR(
      "pollset_kick",
      grpc_pollset_kick(grpc_polling_entity_pollset(&sync->pops), nullptr));
  gpr_mu_unlock(sync->mu);
}

int main(int argc, char** argv) {
  int result = 0;
  grpc_core::ExecCtx exec_ctx;
  synchronizer sync;
  grpc_channel_credentials* creds = nullptr;
  const char* service_url = "https://test.foo.google.com/Foo";
  grpc_auth_metadata_context context;
  gpr_cmdline* cl = gpr_cmdline_create("print_google_default_creds_token");
  grpc_pollset* pollset = nullptr;
  grpc_error* error = nullptr;
  gpr_cmdline_add_string(cl, "service_url",
                         "Service URL for the token request.", &service_url);
  gpr_cmdline_parse(cl, argc, argv);
  memset(&context, 0, sizeof(context));
  context.service_url = service_url;

  grpc_init();

  creds = grpc_google_default_credentials_create();
  if (creds == nullptr) {
    fprintf(stderr, "\nCould not find default credentials.\n\n");
    fflush(stderr);
    result = 1;
    goto end;
  }

  memset(&sync, 0, sizeof(sync));
  pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(pollset, &sync.mu);
  sync.pops = grpc_polling_entity_create_from_pollset(pollset);
  sync.is_done = false;
  GRPC_CLOSURE_INIT(&sync.on_request_metadata, on_metadata_response, &sync,
                    grpc_schedule_on_exec_ctx);

  error = GRPC_ERROR_NONE;
  if (grpc_call_credentials_get_request_metadata(
          (reinterpret_cast<grpc_composite_channel_credentials*>(creds))
              ->call_creds,
          &sync.pops, context, &sync.md_array, &sync.on_request_metadata,
          &error)) {
    // Synchronous response.  Invoke callback directly.
    on_metadata_response(&sync, error);
    GRPC_ERROR_UNREF(error);
  }

  gpr_mu_lock(sync.mu);
  while (!sync.is_done) {
    grpc_pollset_worker* worker = nullptr;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(grpc_polling_entity_pollset(&sync.pops), &worker,
                              GRPC_MILLIS_INF_FUTURE)))
      sync.is_done = true;
    gpr_mu_unlock(sync.mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(sync.mu);
  }
  gpr_mu_unlock(sync.mu);

  grpc_channel_credentials_release(creds);
  gpr_free(grpc_polling_entity_pollset(&sync.pops));

end:
  gpr_cmdline_destroy(cl);
  grpc_shutdown();
  return result;
}
