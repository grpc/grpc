/*
 *
 * Copyright 2017 gRPC authors.
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

#include "test/core/end2end/h2_ssl_cert/ssl_cert_test.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/support/tmpfile.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

extern void simple_request(grpc_end2end_test_config config);

typedef struct fullstack_secure_fixture_data {
  char *localaddr;
} fullstack_secure_fixture_data;

grpc_end2end_test_fixture grpc_end2end_chttp2_create_fixture_secure_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data *ffd =
      gpr_malloc(sizeof(fullstack_secure_fixture_data));
  memset(&f, 0, sizeof(f));

  gpr_join_host_port(&ffd->localaddr, "localhost", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(NULL);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(NULL);

  return f;
}

void grpc_end2end_process_auth_failure(void *state, grpc_auth_context *ctx,
                                       const grpc_metadata *md, size_t md_count,
                                       grpc_process_auth_metadata_done_cb cb,
                                       void *user_data) {
  GPR_ASSERT(state == NULL);
  cb(user_data, NULL, 0, NULL, 0, GRPC_STATUS_UNAUTHENTICATED, NULL);
}

void grpc_end2end_chttp2_init_client_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *client_args,
    grpc_channel_credentials *creds) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  f->client =
      grpc_secure_channel_create(creds, ffd->localaddr, client_args, NULL);
  GPR_ASSERT(f->client != NULL);
  grpc_channel_credentials_release(creds);
}

void grpc_end2end_chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args,
    grpc_server_credentials *server_creds) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, NULL);
  grpc_server_register_completion_queue(f->server, f->cq, NULL);
  GPR_ASSERT(grpc_server_add_secure_http2_port(f->server, ffd->localaddr,
                                               server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void grpc_end2end_chttp2_tear_down_secure_fullstack(
    grpc_end2end_test_fixture *f) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  gpr_free(ffd->localaddr);
  gpr_free(ffd);
}

int grpc_end2end_fail_server_auth_check(grpc_channel_args *server_args) {
  size_t i;
  if (server_args == NULL) return 0;
  for (i = 0; i < server_args->num_args; i++) {
    if (strcmp(server_args->args[i].key, FAIL_AUTH_CHECK_SERVER_ARG_NAME) ==
        0) {
      return 1;
    }
  }
  return 0;
}

static void *tag(intptr_t t) { return (void *)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_time(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_time(void) { return n_seconds_time(5); }

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time(), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture *f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = NULL;
}

static void shutdown_client(grpc_end2end_test_fixture *f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = NULL;
}

static void end_test(grpc_end2end_test_fixture *f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
  grpc_completion_queue_destroy(f->shutdown_cq);
}

static void simple_request_body(grpc_end2end_test_fixture f,
                                grpc_end2end_test_result expected_result) {
  grpc_call *c;
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *cqv = cq_verifier_create(f.cq);
  grpc_op ops[6];
  grpc_op *op;
  grpc_call_error error;

  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr:1234");
  c = grpc_channel_create_call(f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               deadline, NULL);
  GPR_ASSERT(c);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), expected_result == SUCCESS);
  cq_verify(cqv);

  grpc_call_unref(c);
  cq_verifier_destroy(cqv);
}

int main(int argc, char **argv) {
  size_t i;
  FILE *roots_file;
  size_t roots_size = strlen(test_root_cert);
  char *roots_filename;

  grpc_test_init(argc, argv);

  /* Set the SSL roots env var. */
  roots_file =
      gpr_tmpfile("chttp2_simple_ssl_cert_fullstack_test", &roots_filename);
  GPR_ASSERT(roots_filename != NULL);
  GPR_ASSERT(roots_file != NULL);
  GPR_ASSERT(fwrite(test_root_cert, 1, roots_size, roots_file) == roots_size);
  fclose(roots_file);
  gpr_setenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR, roots_filename);

  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_test_fixture f =
        begin_test(configs[i].config, "SSL_CERT_tests", NULL, NULL);

    simple_request_body(f, configs[i].result);
    end_test(&f);
    configs[i].config.tear_down_data(&f);
  }

  grpc_shutdown();

  /* Cleanup. */
  remove(roots_filename);
  gpr_free(roots_filename);

  return 0;
}
