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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "test/core/end2end/cq_verifier.h"

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_from_now(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_from_now(void) {
  return n_seconds_from_now(5);
}

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_client(grpc_end2end_test_fixture* f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = nullptr;
}

static void end_test(grpc_end2end_test_fixture* f) {
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
  /* f->shutdown_cq is not used in this test */
  grpc_completion_queue_destroy(f->shutdown_cq);
}

static void test_early_server_shutdown_finishes_tags(
    grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = begin_test(
      config, "test_early_server_shutdown_finishes_tags", nullptr, nullptr);
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_call* s = reinterpret_cast<grpc_call*>(1);
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;

  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  /* upon shutdown, the server should finish all requested calls indicating
     no new call */
  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(101)));
  grpc_server_shutdown_and_notify(f.server, f.cq, tag(1000));
  CQ_EXPECT_COMPLETION(cqv, tag(101), 0);
  CQ_EXPECT_COMPLETION(cqv, tag(1000), 1);
  cq_verify(cqv);
  GPR_ASSERT(s == nullptr);

  grpc_server_destroy(f.server);

  end_test(&f);
  config.tear_down_data(&f);
  cq_verifier_destroy(cqv);
}

void shutdown_finishes_tags(grpc_end2end_test_config config) {
  test_early_server_shutdown_finishes_tags(config);
}

void shutdown_finishes_tags_pre_init(void) {}
